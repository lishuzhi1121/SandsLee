# iOS的main函数中UIApplicationMain做了什么？Runloop又是什么？有什么用？

打开熟悉的Xcode，不管是新建一个iOS的应用程序还是macOS的命令行程序，Xcode都会默认帮我们生成一个 **main.m** 文件，并且其内部就只有一个 `main函数` ，当然，我们知道这是程序的入口。

iOS应用程序的main函数模版如下：

![ios-main](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-144634-8mLeDK.png)

而macOS命令行程序的main函数模版如下：

![macOS-main](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-144958-jIcb99.png)

很明显，这两个main函数是有差异的，并且图上我也标注出来了。此外，我们还注意到一个现象，就是macOS的命令行程序执行完代码就退出了，而iOS的应用程序启动之后即使没有代码执行了应用程序也不会退出，这是为什么呢？？

从两者的main函数分析，显然，很有可能是iOS的这个 `UIApplicationMain` 函数在 **“作怪”** ，那么它究竟做了什么呢？？

## 一、UIApplicationMain 函数

UIApplicationMain 函数定义在 `UIKit` 框架的 `UIApplication.h` 中，其定义如下：

```objc
// If nil is specified for principalClassName, the value for NSPrincipalClass from the Info.plist is used. If there is no
// NSPrincipalClass key specified, the UIApplication class is used. The delegate class will be instantiated using init.
UIKIT_EXTERN int UIApplicationMain(int argc, char * _Nullable argv[_Nonnull], NSString * _Nullable principalClassName, NSString * _Nullable delegateClassName);
```

> 注释翻译：如果这个函数的 `principalClassName` 参数传空，则会读取 **Info.plist** 中配置的 `NSPrincipalClass` ，如果没有配置，则直接使用 `UIApplication` class。并且 `delegate` 类也会使用它来初始化。

可惜的是UIKit.framework并不开源，我们看不到这个UIApplicationMain函数内部具体实现，不过没有关系，我们还可以通过断点来看相应的汇编代码。

在Xcode中我们添加符号断点 `UIApplicationMain` ，如下图：

![symbols](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-150941-iY6KXW.png)

然后将Xcode的Debug设置为总是显示汇编，如下图：

![debug](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-151328-vAgssL.png)

运行Xcode，启动之后将会来到我们的符号断点 `UIApplicationMain` 函数，如下图：

![UIApplicationMain](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-152105-iShot2020-05-27下午03.18.22.png)

这一段汇编非常长，不过结合注释我们来看几个点：

首先，上面提到关于 `principalClassName` 会从 `Info.plist` 中读取，是否真的是这样？我们往下读这一段汇编，会看到如下代码：

![principalClassName](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-152652-GQqxbq.png)

看来确实是像注释说的那样，去Info.plist中读取了。

接着往下读，我们会发现比较核心的东西，也就是这篇文章的主角 **Runloop** ，如下图：

![runloop](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-153154-icdteQ.png)

也就是说，在这个函数里进行Runloop的操作，其实就是创建了当前线程（主线程）的Runloop，并设置为默认模式，然后运行起来。

这里先说一下结论：**iOS应用程序之所以不会退出就是因为这里的Runloop的存在。** 先不用管Runloop是什么，后续我会细说。

我们继续往下看，还有一个点要提一下：

![application-delegate](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-154840-wfm7Rr.png)

从图上我们可以看到 `UIApplication` 的 `setDelegate:` 也是在这个UIApplicationMain函数中调用的，所以它的调用时机会比 `application:didFinishLaunchingWithOptions:` 要早的多，如有需要可以考虑hook该函数。

## 二、iOS中的Runloop运行循环


