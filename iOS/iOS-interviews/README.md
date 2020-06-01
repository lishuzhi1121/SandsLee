# iOS面试记录

## 一、得物

### 1、atomic/nonatomic 的作用和区别？

### 2、weak关键字

全局hash表存储，weak指向的对象内存地址作为key，weak修饰的属性变量的内存地址作为value（因为每个对象可能有多个 weak 指针，所以这个value的值是 CFMutableSet 类型）。

[weak 关键字](https://www.jianshu.com/p/e786f4173814)

### 3、消息机制

直接参考：[iOS的运行时消息转发机制/转发流程](http://blog.sandslee.com/iOS/iOS-runtime-msgSend/)

### 4、NSTimer需要注意的问题

循环引用、定时不准确、runloop模式、子线程

### 5、socket粘包问题

数据帧结构，缓冲区清空，重连

### 6、RxSwift的driver模型

### 7、Swift的optional类型是如何实现的？

Swift的可选类型，其实是一个枚举型，里面有None和Some两种类型，所谓的nil就是Optional.None,而非nil就是Optional.Some，然后通过Some(T)包装(wrap)原始值，当然有包装肯定要解包，所以在使用可选类型的时候要拆包，也就是从enum中取出来原始值。
参考：[Swift中的可选类型的实现原理](https://blog.csdn.net/chuqingr/article/details/51626816)




