cmake_minimum_required(VERSION 3.0) #要求的系统cmake的最小版本
project(Threadpool)  #给工程一个名字

#配置编译选项  -g 或-O得到 
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g) #表示在原来变量的基础上再加一个-g选项 其中-g表示生成出来的可执行文件可以调试


#配置最终的可执行文件输出的路径  保存到当前目录的bin目录下
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)


#配置头文件的搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)

#加载子目录    也就是后续要cmake的信息要去src目录下找CMakeList.txt
add_subdirectory(src)