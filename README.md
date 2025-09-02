# threadpool
基于可变参模版的高性能c++线程池源代码  
&emsp;线程池功能由/include目录下的threadpool.hpp头文件提供，用户直接将该头文件包含到工程目录，即可使用其提供的便捷接口实现。线程池提供fixed固定线程数模式和cached动态模式，用户可以根据任务负载灵活调整；此外，其他接口有：设置任务队列上限，设置线程池动态模式下线程数上限，启动时初始线程数，以及最重要的SubmitTask接口，用于给用户向任务队列提交需要完成的任务。SubmitTask接口基于可变参模版、引用折叠等技术实现，支持用户提交任意类型函数和任意类型和数量的参数，并基于future和packaged_task实现异步结果获取，用户主线程提交任务后可以做其他事情，任务的调度与执行完全由线程池内部完成，此外用户主线程只需要调用get()即可等待获取结果。Cached模式下，实现线程空闲超时回收策略，避免过多线程资源长期闲置。

编译项：./autobuild.sh  自动编译脚本
&emsp;或者  
&emsp;&emsp;cd build/  
&emsp;&emsp;rm -rf *  
&emsp;&emsp;cmake ..  
&emsp;&emsp;make  

目录介绍：  
&emsp;bin/  包含了项目编译的可执行文件 
&emsp;include/  包含了线程池threadpool.hpp文件，提供线程池的所有功能与接口
&emsp;build/  包含了项目编译过程中的中间文件  
&emsp;src/  包含了源文件，为一个测试cpp文件  
&emsp;CmakeLists.txt  为cmake构建信息  
&emsp;autobuild.sh  为自动化编译脚本 
