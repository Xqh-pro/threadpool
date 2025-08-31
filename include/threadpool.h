#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>  //vector容器用来存放线程
#include <queue>  //用来存放任务队列
#include <memory>  //准备使用强智能指针，延长声明周期，并能自动释放
#include <atomic>   //使用原子类型，确保线程安全
//外部用户以及内部的线程池都会操作这个任务队列，需要用到线程的通信机制
#include <mutex>  //线程通信机制--互斥锁
#include <condition_variable>  //线程通信机制--条件变量
#include <functional>  
#include <unordered_map>
#include <thread>
#include <future>

const int TASK_MAX_THRESHHOLD = 2;//INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;//cached模式下线程空闲时间上限为60s


//线程池支持的两种模式
enum class PoolMode {
	MODE_FIXED, //固定线程数量的模式
	MODE_CACHED,  //线程数量可动态增长的模式
};


//线程类型
class Thread {
public:
	//线程函数对象
	using ThreadFunc = std::function<void(int)>;
	//线程构造函数
	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{}
	//线程析构函数
	~Thread() = default;
	//启动线程
	void start()
	{
		std::thread t(func_, threadId_); //创建线程对象实例的同时，启动线程执行func_函数
		t.detach();
	}
	//获取线程id
	int getId() const
	{
		return threadId_;
	}

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; //保存线程id
};
int Thread::generateId_ = 0;





//线程池类型
class ThreadPool {
public:
	ThreadPool()
		:initThreadSize_(0)  //初始线程数量
		, idleThreadSize_(0)  //当前空闲线程的数量
		, threadSizeThresHold_(THREAD_MAX_THRESHHOLD)  //能支持的线程数上限
		, curThreadSize_(0) //当前线程总数
		, taskSize_(0)  //初始任务数量，刚开始时当然是0
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD) //任务队列任务数量上限
		, poolMode_(PoolMode::MODE_FIXED)  //线程池工作模式，为默认的固定模式
		, isPoolRunning_(false)
	{}
	~ThreadPool()
	{
		isPoolRunning_ = false;
		//等待线程池里面的所有线程都返回了再析构
		//线程要么在阻塞  要么在忙
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();//唤醒，各线程从等待状态变为阻塞状态
		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	}

	//设置线程池的工作模式
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}


	//设置task任务队列上限的阈值
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		taskQueMaxThreshHold_ = threshhold;
	}

	//设置线程池cached模式下线程数量的上限值
	void setThreadSizeThresHold(int threshhold) {
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED) {
			threadSizeThresHold_ = threshhold;
		}//cached模式下才能设置，否则等于没做
	}

	//给线程池提交任务
	//*****该用可变参模版编程*****//
	//让submitTask可接受任意任务函数和任意数量的参数 
	//告诉编译器，Func是第一个参数，可以是任意类型
	//而Args是一个模版参数包，表示0或多个额外的参数
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//打包任务，放入任务队列里面
		using RType = decltype(func(args...)); //RType为类型
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		//获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回

		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; })) {
			//如果返回false，说明等待超时了，notFull_依然没有满足
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); });//打包任务
			(*task)();//执行一下该空任务，不然.get()要一直等到任务返回值
			return task->get_future();
		}
		//3、如果有空余  将任务放入任务队列
		//taskQue_.emplace(sp);
		//using Task = std::function<void()>;
		taskQue_.emplace([task]() {
				//执行下面的任务
				(*task)();
			});
		 
		taskSize_++;
		//新放了任务进来，队列肯定不空，在条件变量notEmpty_上通知
		notEmpty_.notify_all();  //通知消费者


		//cached模式：根据任务数量和空闲线程数量，判断是否需要额外动态增加线程？
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThresHold_) {//动态创建模式下有上限阈值
			std::cout << ">>> create new thread..." << std::endl;

			//那么创建新线程
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1)); //把new出来的裸指针包进智能指针std::unique_ptr<Thread>里，名字叫做ptr 是一个独占式智能指针，指向刚生成的Thread对象
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();//启动新创建的线程
			idleThreadSize_++;
			curThreadSize_++;
		}

		//返回任务的Result对象
		return result;
	}


	//开启线程池  初始化为当前cpu的核心数量
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		isPoolRunning_ = true;

		initThreadSize_ = initThreadSize;  //线程数量初始值
		curThreadSize_ = initThreadSize; //当前线程总数

		//创建线程对象   根据用户指定的线程数，动态创建线程对象，将
		//指针存储到数组threads_里
		for (int i = 0; i < initThreadSize_; i++) {
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1)); //把new出来的裸指针包进智能指针std::unique_ptr<Thread>里，名字叫做ptr 是一个独占式智能指针，指向刚生成的Thread对象
			//注意unique_ptr是不允许进行普通的拷贝构造的
			//因此，需要使用std::move将独占权交给vector先
			//threads_.emplace_back(std::move(ptr));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr)); //这是map，不是vector了
		}

		//启动所有线程
		for (int i = 0; i < initThreadSize_; i++) {
			threads_[i]->start();   //Thread是自定义的线程类
			idleThreadSize_++; //每启动一个线程，那么就多一个工人
		}
	}

	//禁止用户对线程池使用拷贝构造函数
	ThreadPool(const ThreadPool&) = delete;
	//禁止用户对线程池使用拷贝赋值运算符  
	ThreadPool& operator=(const ThreadPool) = delete;
private:
	//定义线程函数，之所以要在线程池类里实现，是因为需要访问该类的私有成员
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		//为fasle的话，那么线程就自动回收销毁了
		//注意：改进：要等所有任务都被完成了，线程池才可以回收所有线程资源
		for (;;) {
			Task task;//默认为空
			{
				//获取锁   注意该锁只是用于访问任务队列是确保线程安全的
				//如果此时锁被别的线程拿着，就会在这里堵塞，直到拿到锁代码才会继续往下走
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid: " << std::this_thread::get_id()
					<< "尝试获取任务..." << std::endl;

				//如在cached模式下，有可能创建了很多线程，但若这些线程空闲时间超过60s,
				//应该把多余的线程（超过initThreadSize_的）回收掉
				while (taskQue_.size() == 0) {//如果没有任务，就等待一定时间，超时就回收看看是否线程

					//直到任务队列没有任务了，才允许去判断要不要结束线程
					if (!isPoolRunning_) {
						threads_.erase(threadid);
						std::cout << "threadid: " << std::this_thread::get_id() << " exit!!!" << std::endl;
						exitCond_.notify_all();
						return;
					}

					if (poolMode_ == PoolMode::MODE_CACHED) {
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
							//超时返回
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
								//开始回收当前线程,不过最低删到初始固定线程数，不能再减了
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadid: " << std::this_thread::get_id() << " exit!!!" << std::endl;
								return;//结束掉线程的运行
							}
						}
					}
					else {
						notEmpty_.wait(lock);

					}

				}//局部对象lock出了作用域会自动析构，这样就会自动释放锁了


				idleThreadSize_--; //线程被分配走一个，处于忙
				std::cout << "tid: " << std::this_thread::get_id()
					<< "获取任务成功..." << std::endl;
				//从任务队列中取一个任务出来
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				if (taskQue_.size() > 0) {
					//如果依然有任务，那就通知唤醒其他线程（消费者）执行任务
					notEmpty_.notify_all();
				}

				//拿走了一个任务，得进行通知（生产者）
				notFull_.notify_all();
			}

			//当前线程负责执行这个任务  访问任务队列完了应该释放锁再执行任务
				//应该你执行任务是执行自己的，不能把锁占着不让别的线程去同样操作队列
			if (task != nullptr) {
				task();//直接执行funtion<void()>匿名函数对象
			}
			idleThreadSize_++;//任务做完，线程从忙变回空闲
			//更新时间
			lastTime = std::chrono::high_resolution_clock().now();

		}
	}
	//检查Pool运行状态
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}//const表示函数不修改任何对象成员变量（编译器负责阻止）
private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	
	int initThreadSize_; //初始的线程数量
	int threadSizeThresHold_;  //cached模式下线程数量上限阈值，即动态创建增加线程也得有度
	std::atomic_int curThreadSize_;//当前线程池的线程实时数量
	std::atomic_int idleThreadSize_;//记录空闲线程的数量

	//任务就是函数对象 
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;//任务队列
	std::atomic_int taskSize_;  //任务的数量    原子类型，确保线程安全，因为++和--操作分别是用户线程和线程池线程,所以使用原子操作
	int taskQueMaxThreshHold_;  //任务队列的上限数量

	std::mutex taskQueMtx_;   //声明一个互斥量，即一把锁
	//条件变量是和互斥量一起使用的的等待/通知机制
	std::condition_variable notFull_;  //表示任务队列不满
	std::condition_variable notEmpty_;  //表示任务队列不空
	std::condition_variable exitCond_;  //等待线程资源全部回收

	PoolMode poolMode_;  //记录当前线程池的工作模式
	//表示当前线程池的启动状态
	std::atomic_bool isPoolRunning_;

};


#endif 
