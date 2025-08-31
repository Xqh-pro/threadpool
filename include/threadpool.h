#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>  //vector������������߳�
#include <queue>  //��������������
#include <memory>  //׼��ʹ��ǿ����ָ�룬�ӳ��������ڣ������Զ��ͷ�
#include <atomic>   //ʹ��ԭ�����ͣ�ȷ���̰߳�ȫ
//�ⲿ�û��Լ��ڲ����̳߳ض���������������У���Ҫ�õ��̵߳�ͨ�Ż���
#include <mutex>  //�߳�ͨ�Ż���--������
#include <condition_variable>  //�߳�ͨ�Ż���--��������
#include <functional>  
#include <unordered_map>
#include <thread>
#include <future>

const int TASK_MAX_THRESHHOLD = 2;//INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;//cachedģʽ���߳̿���ʱ������Ϊ60s


//�̳߳�֧�ֵ�����ģʽ
enum class PoolMode {
	MODE_FIXED, //�̶��߳�������ģʽ
	MODE_CACHED,  //�߳������ɶ�̬������ģʽ
};


//�߳�����
class Thread {
public:
	//�̺߳�������
	using ThreadFunc = std::function<void(int)>;
	//�̹߳��캯��
	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{}
	//�߳���������
	~Thread() = default;
	//�����߳�
	void start()
	{
		std::thread t(func_, threadId_); //�����̶߳���ʵ����ͬʱ�������߳�ִ��func_����
		t.detach();
	}
	//��ȡ�߳�id
	int getId() const
	{
		return threadId_;
	}

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; //�����߳�id
};
int Thread::generateId_ = 0;





//�̳߳�����
class ThreadPool {
public:
	ThreadPool()
		:initThreadSize_(0)  //��ʼ�߳�����
		, idleThreadSize_(0)  //��ǰ�����̵߳�����
		, threadSizeThresHold_(THREAD_MAX_THRESHHOLD)  //��֧�ֵ��߳�������
		, curThreadSize_(0) //��ǰ�߳�����
		, taskSize_(0)  //��ʼ�����������տ�ʼʱ��Ȼ��0
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD) //�������������������
		, poolMode_(PoolMode::MODE_FIXED)  //�̳߳ع���ģʽ��ΪĬ�ϵĹ̶�ģʽ
		, isPoolRunning_(false)
	{}
	~ThreadPool()
	{
		isPoolRunning_ = false;
		//�ȴ��̳߳�����������̶߳�������������
		//�߳�Ҫô������  Ҫô��æ
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();//���ѣ����̴߳ӵȴ�״̬��Ϊ����״̬
		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	}

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}


	//����task����������޵���ֵ
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		taskQueMaxThreshHold_ = threshhold;
	}

	//�����̳߳�cachedģʽ���߳�����������ֵ
	void setThreadSizeThresHold(int threshhold) {
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED) {
			threadSizeThresHold_ = threshhold;
		}//cachedģʽ�²������ã��������û��
	}

	//���̳߳��ύ����
	//*****���ÿɱ��ģ����*****//
	//��submitTask�ɽ������������������������Ĳ��� 
	//���߱�������Func�ǵ�һ����������������������
	//��Args��һ��ģ�����������ʾ0��������Ĳ���
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//������񣬷��������������
		using RType = decltype(func(args...)); //RTypeΪ����
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		//��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//�û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����

		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; })) {
			//�������false��˵���ȴ���ʱ�ˣ�notFull_��Ȼû������
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); });//�������
			(*task)();//ִ��һ�¸ÿ����񣬲�Ȼ.get()Ҫһֱ�ȵ����񷵻�ֵ
			return task->get_future();
		}
		//3������п���  ����������������
		//taskQue_.emplace(sp);
		//using Task = std::function<void()>;
		taskQue_.emplace([task]() {
				//ִ�����������
				(*task)();
			});
		 
		taskSize_++;
		//�·���������������п϶����գ�����������notEmpty_��֪ͨ
		notEmpty_.notify_all();  //֪ͨ������


		//cachedģʽ���������������Ϳ����߳��������ж��Ƿ���Ҫ���⶯̬�����̣߳�
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThresHold_) {//��̬����ģʽ����������ֵ
			std::cout << ">>> create new thread..." << std::endl;

			//��ô�������߳�
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1)); //��new��������ָ���������ָ��std::unique_ptr<Thread>����ֽ���ptr ��һ����ռʽ����ָ�룬ָ������ɵ�Thread����
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();//�����´������߳�
			idleThreadSize_++;
			curThreadSize_++;
		}

		//���������Result����
		return result;
	}


	//�����̳߳�  ��ʼ��Ϊ��ǰcpu�ĺ�������
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		isPoolRunning_ = true;

		initThreadSize_ = initThreadSize;  //�߳�������ʼֵ
		curThreadSize_ = initThreadSize; //��ǰ�߳�����

		//�����̶߳���   �����û�ָ�����߳�������̬�����̶߳��󣬽�
		//ָ��洢������threads_��
		for (int i = 0; i < initThreadSize_; i++) {
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1)); //��new��������ָ���������ָ��std::unique_ptr<Thread>����ֽ���ptr ��һ����ռʽ����ָ�룬ָ������ɵ�Thread����
			//ע��unique_ptr�ǲ����������ͨ�Ŀ��������
			//��ˣ���Ҫʹ��std::move����ռȨ����vector��
			//threads_.emplace_back(std::move(ptr));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr)); //����map������vector��
		}

		//���������߳�
		for (int i = 0; i < initThreadSize_; i++) {
			threads_[i]->start();   //Thread���Զ�����߳���
			idleThreadSize_++; //ÿ����һ���̣߳���ô�Ͷ�һ������
		}
	}

	//��ֹ�û����̳߳�ʹ�ÿ������캯��
	ThreadPool(const ThreadPool&) = delete;
	//��ֹ�û����̳߳�ʹ�ÿ�����ֵ�����  
	ThreadPool& operator=(const ThreadPool) = delete;
private:
	//�����̺߳�����֮����Ҫ���̳߳�����ʵ�֣�����Ϊ��Ҫ���ʸ����˽�г�Ա
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		//Ϊfasle�Ļ�����ô�߳̾��Զ�����������
		//ע�⣺�Ľ���Ҫ���������񶼱�����ˣ��̳߳زſ��Ի��������߳���Դ
		for (;;) {
			Task task;//Ĭ��Ϊ��
			{
				//��ȡ��   ע�����ֻ�����ڷ������������ȷ���̰߳�ȫ��
				//�����ʱ��������߳����ţ��ͻ������������ֱ���õ�������Ż����������
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid: " << std::this_thread::get_id()
					<< "���Ի�ȡ����..." << std::endl;

				//����cachedģʽ�£��п��ܴ����˺ܶ��̣߳�������Щ�߳̿���ʱ�䳬��60s,
				//Ӧ�ðѶ�����̣߳�����initThreadSize_�ģ����յ�
				while (taskQue_.size() == 0) {//���û�����񣬾͵ȴ�һ��ʱ�䣬��ʱ�ͻ��տ����Ƿ��߳�

					//ֱ���������û�������ˣ�������ȥ�ж�Ҫ��Ҫ�����߳�
					if (!isPoolRunning_) {
						threads_.erase(threadid);
						std::cout << "threadid: " << std::this_thread::get_id() << " exit!!!" << std::endl;
						exitCond_.notify_all();
						return;
					}

					if (poolMode_ == PoolMode::MODE_CACHED) {
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
							//��ʱ����
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
								//��ʼ���յ�ǰ�߳�,�������ɾ����ʼ�̶��߳����������ټ���
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadid: " << std::this_thread::get_id() << " exit!!!" << std::endl;
								return;//�������̵߳�����
							}
						}
					}
					else {
						notEmpty_.wait(lock);

					}

				}//�ֲ�����lock������������Զ������������ͻ��Զ��ͷ�����


				idleThreadSize_--; //�̱߳�������һ��������æ
				std::cout << "tid: " << std::this_thread::get_id()
					<< "��ȡ����ɹ�..." << std::endl;
				//�����������ȡһ���������
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				if (taskQue_.size() > 0) {
					//�����Ȼ�������Ǿ�֪ͨ���������̣߳������ߣ�ִ������
					notEmpty_.notify_all();
				}

				//������һ�����񣬵ý���֪ͨ�������ߣ�
				notFull_.notify_all();
			}

			//��ǰ�̸߳���ִ���������  ���������������Ӧ���ͷ�����ִ������
				//Ӧ����ִ��������ִ���Լ��ģ����ܰ���ռ�Ų��ñ���߳�ȥͬ����������
			if (task != nullptr) {
				task();//ֱ��ִ��funtion<void()>������������
			}
			idleThreadSize_++;//�������꣬�̴߳�æ��ؿ���
			//����ʱ��
			lastTime = std::chrono::high_resolution_clock().now();

		}
	}
	//���Pool����״̬
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}//const��ʾ�������޸��κζ����Ա������������������ֹ��
private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	
	int initThreadSize_; //��ʼ���߳�����
	int threadSizeThresHold_;  //cachedģʽ���߳�����������ֵ������̬���������߳�Ҳ���ж�
	std::atomic_int curThreadSize_;//��ǰ�̳߳ص��߳�ʵʱ����
	std::atomic_int idleThreadSize_;//��¼�����̵߳�����

	//������Ǻ������� 
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;//�������
	std::atomic_int taskSize_;  //���������    ԭ�����ͣ�ȷ���̰߳�ȫ����Ϊ++��--�����ֱ����û��̺߳��̳߳��߳�,����ʹ��ԭ�Ӳ���
	int taskQueMaxThreshHold_;  //������е���������

	std::mutex taskQueMtx_;   //����һ������������һ����
	//���������Ǻͻ�����һ��ʹ�õĵĵȴ�/֪ͨ����
	std::condition_variable notFull_;  //��ʾ������в���
	std::condition_variable notEmpty_;  //��ʾ������в���
	std::condition_variable exitCond_;  //�ȴ��߳���Դȫ������

	PoolMode poolMode_;  //��¼��ǰ�̳߳صĹ���ģʽ
	//��ʾ��ǰ�̳߳ص�����״̬
	std::atomic_bool isPoolRunning_;

};


#endif 
