#这里才有源码，才要进行将所有源码来生成可执行文件

#当前目录下所有代码源文件
aux_source_directory(. SRC_LIST)

#生成可执行文件ChatServer    由SRC_LIST表示的所有文件
add_executable(main ${SRC_LIST})  

#指定可执行文件链接时需要依赖的库文件
target_link_libraries(main)