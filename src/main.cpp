// 线程池项_最终版.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <functional>
#include <thread>
#include <future>
#include <chrono>
using namespace std;

#include "threadpool.h"

int sum1(int a, int b) {
    this_thread::sleep_for(chrono::seconds(5));
    return a + b;
}

int sum2(int a, int b, int c) {
    this_thread::sleep_for(chrono::seconds(5));
    return a + b + c;
}

/*
* //如何能让线程池提交任务更方便
* 1、pool.submitTask(sum1,1,2);
* 2、pool.submitTask(sum2,1,2,3);
* 可变参模版编程 
* 
* 2、为了接收返回值，我们自己造了一个result以及相关类型，代码很多
* 但c++11线程库 thread(无法获得返回值)  packaged_task(在future头文件)提供了获取任务返回值的途径
* 使用future来代替result，节省代码
*/
int main()
{
    //    thread t1(sum1, 1, 2);
    //    thread t2(sum2, 1, 2, 3);
    //
    //    t1.join(); //阻塞当前线程知道线程结束
    //    t2.join();
    //

    //packaged_task<int(int, int)> task(sum1);
    ////future其实就是result
    //future<int> res = task.get_future();
    ////task(10, 20);
    //thread t(std::move(task), 10, 20);
    //t.detach();
    //cout << res.get() << endl;

    ThreadPool pool;
    //pool.setMode(PoolMode::MODE_CACHED);
    pool.start(2);
    future<int> r1=  pool.submitTask(sum1, 1, 2);
    future<int> r2 = pool.submitTask(sum2, 1, 2,3);
    future<int> r3 = pool.submitTask(sum1, 1, 2);
    future<int> r4 = pool.submitTask(sum1, 1, 2);
    future<int> r5 = pool.submitTask(sum1, 1, 2);
    //future<int> r6 = pool.submitTask(sum1, 1, 2);

    cout << r1.get() << endl;
    cout << r2.get() << endl;
    cout << r3.get() << endl;
    cout << r4.get() << endl;
    cout << r5.get() << endl;
    //cout << r6.get() << endl;
}

