# coroutine
Coroutine in c++ / 协程

c++ 实现的协程

### 测试 

> echo服务器 

> g++ -c coroutine.cpp main.cpp -std=c++11

> nc 127.0.0.1 8080

### TODO

- 修改上下文切换，替换ucontext

- 改进栈保存，使用内存池

- 修改／增加网络函数


### 参考

https://github.com/chunyi1994/coroutine 

https://github.com/wangbojing/NtyCo

https://github.com/yyrdl/libco-code-study/

