# llvm-example

llvm 版本：
14.0.6

编译：
sh compile.sh

运行：
./main

1.读取 json，创建 IR 结构体
2.创建函数，初始化结构体对象，返回对象指针
3.调用 LLJIT 引擎执行函数，获取对象指针
