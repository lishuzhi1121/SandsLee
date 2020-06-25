# iOS面试记录

## 通用计算机技术

### 1、HTTP OSI七层网络模型 & TCP/IP四层网络模型

OSI七层网络模型参考下图：
![osi](https://raw.githubusercontent.com/lishuzhi1121/LinuxTutorial/master/images/ISO:OSI%E4%B8%83%E5%B1%82%E7%BD%91%E7%BB%9C%E6%A8%A1%E5%9E%8B.png)

TCP/IP 四层网络模型参考下图：
![tcpip](https://raw.githubusercontent.com/lishuzhi1121/LinuxTutorial/master/images/IMG_A479D37BC665-1.jpeg)

### 2、HTTP请求响应时间优化，DNS查询方式

HTTP请求响应时间优化：
* 结合实际业务，将不需要使用域名的直接改成IP，省去DNS查询（正常情况下，请求大部分时间消耗在DNS查询上），基本方案：本地维护一个ip列表，直接使用ip进行请求，而非用域名，并定期去服务器更新这个列表。同时还可以周期性的ping一下列表上的ip，动态选取延迟最小的ip。
* 优化DNS解析，例如：HTTPDNS之类的技术方案解决，或者改用socket方式通信等
* 根据网络情况，动态调整超时时间，可有效减少不必要的等待及重复请求；
* 根据实际业务，适当合并多个接口到一个，避免同时并发多个http请求；
* 根据情况客户端可以适当进行自动重发，此时 **服务端需要注意请求的幂等性**

DNS查询方式参考下图：
![dns](https://raw.githubusercontent.com/lishuzhi1121/LinuxTutorial/master/images/IMG_1867.PNG)

这种方式叫 **迭代查询** 。


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


## 二、声网 Agora

### 1、runloop 常驻线程

### 2、runtime 反射

### 3、KVC 实现流程

##### setValue:forKey：

调用`setValue:forKey:`方法，首先会按照`setKey:`、`_setKey:`顺序查找方法:
1.找到了方法：直接传递参数调用方法设值;
2.没找到方法：会去查看`+(BOOL)accessInstanceVariablesDirectly`方法的返回值，该方法表示是否可以直接设置成员变量的值。
返回NO：调用`setValue:forUndefinedKey:`并抛出异常NSUnkonwnKeyException；
返回YES：会按照_key、_isKey、key、isKey顺序查找成员变量，如果找到成员变量直接赋值，没有找到同样抛出异常NSUnkonwnKeyException。

参考：[KVC实现原理](https://www.meiwen.com.cn/subject/qxvxoqtx.html)

### 4、block都有哪些类型

globalBlock、stackBlock 、mallocBlock 

参考：[iOS Block的三种类型](https://www.jianshu.com/p/4b4e280f3f81)

### 5、__block 底层实现原理

通过 `clang --rewrite-objc` 可以知道其实__block修饰的变量会被包装成一个结构体，变量成为结构体成员。

```objc
struct __Block_byref_count_0 {
  void *__isa;
__Block_byref_count_0 *__forwarding;
 int __flags;
 int __size;
 int count;
};

struct __main_block_impl_0 {
  struct __block_impl impl;
  struct __main_block_desc_0* Desc;
  __Block_byref_count_0 *count; // by ref
  __main_block_impl_0(void *fp, struct __main_block_desc_0 *desc, __Block_byref_count_0 *_count, int flags=0) : count(_count->__forwarding) {
    impl.isa = &_NSConcreteStackBlock;
    impl.Flags = flags;
    impl.FuncPtr = fp;
    Desc = desc;
  }
};
static void __main_block_func_0(struct __main_block_impl_0 *__cself) {
  __Block_byref_count_0 *count = __cself->count; // bound by ref

        (count->__forwarding->count)++;
        NSLog((NSString *)&__NSConstantStringImpl__var_folders_lm_0sgjskj102d9k5_ypqff3bgh0000gn_T_main_296d47_mi_0, (count->__forwarding->count));
    }
static void __main_block_copy_0(struct __main_block_impl_0*dst, struct __main_block_impl_0*src) {_Block_object_assign((void*)&dst->count, (void*)src->count, 8/*BLOCK_FIELD_IS_BYREF*/);}

static void __main_block_dispose_0(struct __main_block_impl_0*src) {_Block_object_dispose((void*)src->count, 8/*BLOCK_FIELD_IS_BYREF*/);}

static struct __main_block_desc_0 {
  size_t reserved;
  size_t Block_size;
  void (*copy)(struct __main_block_impl_0*, struct __main_block_impl_0*);
  void (*dispose)(struct __main_block_impl_0*);
} __main_block_desc_0_DATA = { 0, sizeof(struct __main_block_impl_0), __main_block_copy_0, __main_block_dispose_0};
int main(int argc, char * argv[]) {
    // 变量定义：int count = 0;
    __attribute__((__blocks__(byref))) __Block_byref_count_0 count = {(void*)0,(__Block_byref_count_0 *)&count, 0, sizeof(__Block_byref_count_0), 0};
    // block定义
    void(*block)(void) = ((void (*)())&__main_block_impl_0((void *)__main_block_func_0, &__main_block_desc_0_DATA, (__Block_byref_count_0 *)&count, 570425344));
    ((void (*)(__block_impl *))((__block_impl *)block)->FuncPtr)((__block_impl *)block);
    return 0;
}
```



## 三、Soul

### 1、不可变字符串用strong修饰的话有什么问题？

strong是引用类型，浅拷贝，指针引用，如果使用strong修饰，那么将一个字符串赋值给两个对象的strong类型的字符串属性时，字符串一变则两个对象的属性值都变了，因为只是地址引用。

参考：[理解iOS中深浅拷贝-为什么NSString使用copy](https://www.jianshu.com/p/eda4957735ee)

### 2、block循环引用解决方案？__strong 修饰 __weak的对象引用计数会+1吗？如果会，什么时候+1？

循环引用解决：__weak、__unsafe_unretained、@weakify和@strongify

参考：[iOS 区块(Block)循环引用，看我就对了](https://www.jianshu.com/p/9f61eade1ec3)

关于__strong 修饰引用计数问题请看下图：
![](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200601-214950-IMG_1033.PNG)
![](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200601-214830-IMG_1032.JPG)
![](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200601-215033-IMG_1034.JPG)

所以结论是，**这个block调用前这个对象被释放了，那么，strong的时候也是nil。如果没被释放，strong的引用计数+1**。

### 3、SDWebImage实现原理？加载超大图片10000*10000像素的图片会怎么样？底层对图片解码和不解码有什么区别？

架构及缓存设计原理参考：[SDWebImage原理](https://www.jianshu.com/p/647721fb43e7)

加载超大图片会导致内存爆增，从而引发crash

提前解码主要是为了提升渲染效率，解码流程参考：[SDWebImage-解码、压缩图像](https://blog.csdn.net/ZCMUCZX/article/details/79505186)


### 4、tableView的滑动卡顿，优化方案？离屏渲染和高度缓存哪一个对卡顿影响较大？


### 5、可变数组用copy修饰会有什么问题？

### 现场面试（3轮技术+1轮总监+1轮HR）

#### 1、多线程：任务串行，任务依赖，锁相关

iOS各种锁：OSSpinLock(已过期,容易出现优先级反转问题), pthread_mutex, pthread_rwlock, dispatch_semphore, NSLock, NSConditionLock 等，参考：[iOS中的各种锁](https://www.jianshu.com/p/42942c38ee74)

注意⚠️：dispatch_semphore用于异步任务时，wait的队列如果是串行队列，则异步任务的回调不能在同一个串行队列，因为在回调中才会signal，wait会卡线程，如果在同一个串行队列就会出现线程卡死的现象（ **尤其是主线程** ）。

#### 2、Socket数据粘包处理，缓冲区多读单写实现，服务异常监测，CFStream实现Socket通信流程即架构设计

服务端异常监测：比如服务端因为一些原因socket服务端开了，那么客户端如何感知？如何处理？再比如客户端网络突然断掉，这个时候服务端并不知道，那么会发生什么？服务端一般怎么处理？

CFStream实现socket通信架构：我们这边的实现是底层通过runloop实现一个常驻线程，开启socket连接其实表现为开启这个常驻线程，具体可以参考公共库TCP实现源码。

#### 3、算法：翻转字符串

手写算法实现，语言不限

参考leetcode：[151. 翻转字符串里的单词](https://leetcode-cn.com/problems/reverse-words-in-a-string/)

#### 4、现场编码，写一个自定义的Alert，支持文字、图片、自定义按钮等

#### 5、iOS中的证书包含了哪些信息？描述文件又包含哪些信息？Xcode的xctool有一个xcbuild命令行工具，通过它将app导出ipa包的时候需要依赖什么？

#### 6、题外话：为什么可乐、雪碧等汽水的包装是圆的，而牛奶的包装是方的？

#### 7、业务总监：假如用户反馈说在使用你们app的时候手机特别烫，此时上级让你去处理这个问题，你会怎么做？

#### 8、业务总监：聊一聊你做iOS开发五年了，自己在这5年里有什么变化？

#### 9、业务总监：你觉得一个iOS工程师资深、高级和中级有什么区别？

#### 10、业务总监：你有读过哪些技术相关的书籍？对这些书你的看法是什么？从中学到了什么？

#### 11、业务总监：为什么你现在的期望薪资是xx？？

## 四、阿里 ICBU

#### 算法：可以装多少滴水？

在一x、y轴平面上，给一有n(n>=2)个三元组的数组rects: 
[(x11, x12, h1), ..., (xi1, xi2, hi), (xj1, xj2, hj), ..., (xn1, xn2, hn)]，
对于数组中任意两个相邻三元组 (xi1, xi2, hi), (xj1, xj2, hj)，
有：0 < xi1 < xi2 <= xj1 < xj2; 0 < hi; 0 < hj; 
每个三元组对应平面上一矩形，矩形之间的 ***缝隙*** 可以装水，
重力向下，每 **1*1** 面积装一滴水，求可以装多少滴水？

例子
如，输入数组为：[(1,2,2), (3,4,3), (5,8,1), (9,10,2)]
输出：9

![](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/2020/06/25-074140-PastedGraphic-1.png)

此题暴力解法思路就是从数组中第一个元素开始，向后找比它高的矩形，找到则计算两个矩形之间的装水量，否则找出它后面最高的矩形，再计算装水量，然后从找到的最高矩形开始，继续找比它高的矩形，以此类推，直到数组遍历结束。示例代码如下：

```swift
typealias WarterElement = (Int, Int, Int)

func warter(_ a : [WarterElement]) -> Int {
    let warterList : [[Int]] = waterList(a);
    var all = 0
    for i in warterList {
        let f = a[i.first!]
        let l = a[i.last!]
        var v = min(l.2, f.2) * (l.0 - f.1)
        if i[1] - i[0] > 1 {
            for j in i[0] + 1...i[1] - 1 {
                let m = a[j]
                let vr = (m.1-m.0) * m.2
                if vr > 0 {
                    v = v - vr
                }
            }
        }

        all = all + v
    }
    return all;
}

func waterList(_ a :[WarterElement]) -> [[Int]] {
    var list : [[Int]] = [];
    var element = [0];

    var i = 1;
    let count = a.count;
    while i < count {
        let index1 : WarterElement = a[element.first!]
        var j = i
        var secondLarge = j;
        while true {
            let index2 : WarterElement = a[j]
            print("\(index1) ::: \(index2)")
            if (index2.2 >= index1.2) {
                element.append(j)
                list.append(element)
                element = [j]
                i = j;
                break;
            }else{
                if a[secondLarge].2 < a[j].2 {
                    secondLarge = j
                }
                j = j + 1
                if j >= count {
                    element.append(secondLarge)
                    list.append(element)
                    element = [secondLarge]
                    i = secondLarge
                    break
                }
            }
        }
        i = i + 1
    }
    return list
}

print(warter([(1,2,2),(3,4,3),(5,8,1),(9,10,2)]))

```

这种方式虽然思路比较简单，但是计算上相对复杂，循环控制，面积又加右减，容易出错，下面我参考了leetcode的一个题目：[42. 接雨水](https://leetcode-cn.com/problems/trapping-rain-water/)

与这一题很相似，只不过是对给定的数据源描述不同，那么我的思路就是将数据源转换为x轴对应的高度描述数组，从而解题，具体代码与注释详见：[Water](https://github.com/lishuzhi1121/SandsLee/tree/master/DataStructure%26Algorithm/Water/)
