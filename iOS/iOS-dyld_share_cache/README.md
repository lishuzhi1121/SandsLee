# iOS动态共享缓存库抽取方案

苹果从 iOS3.1 开始，为了提高性能，绝大部分系统动态库文件都打包存放到了一个缓存文件中(动态共享缓存：dyld shared cache)，动态共享缓存文件的存放路径为：/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armX （X表示CPU架构）

![dyld_share_cache_armX](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-162939-dyld_share_cache_armX.png)

## 1、动态共享缓存图示

传统方案下，多个App都需要使用到 UIKit 或者 MapKit 时，查找策略如下图：

![app-use1](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-163321-app_use_dyld_share_cache1.png)

加入动态共享缓存库之后，多个App的系统库使用策略将变为下图：

![app-use2](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-163730-app_use_dyld_share_cache2.png)

显然，两者相比的好处是：**共用的一些信息内容可以存放到动态共享缓存中，例如一些header文件信息等，有效提升调用速度** 。

## 2、动态库的加载

不管是在iOS还是在macOS中，都是使用了 /usr/bin/dyld 程序来加载动态库的。dyld 我们常称之为动态链接器。

## 3、从动态共享缓存库中抽取动态库

虽然现在的ida、Hopper等反编译工具都可以识别动态共享缓存库了，但是armv7、armv7s动辄都七八百兆的大小，arm64、arm64e更是随随便便就一个多G，如果太任性的往ida或者Hopper里拖，那就要看你电脑的实力了。

另外，更重要的是，有的时候我们只是想要看其中的某一个动态库的某一个方法内部大概是怎么实现的而已，难道要搞这么大动静吗？

显然，不是这样的，于是乎我们就想着要是能把这个动态共享缓存库给拆分成一个一个的系统动态库，那看起来不就舒服了吗？

其实，这个是可以做到的。而且很简单。

### 3.1 动态共享缓存库的获取

获取动态共享缓存有两种方案：

* 通过越狱手机获取：/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armX
* 通过穿越iPhone firmware 的一些网站下载固件包，例如：[ipsw.me](https://ipsw.me/)

> 这里建议采用下载固件包的方式，毕竟简单方便。
> 另外，**请下载iOS系统版本较低的固件包**（本文使用的是iPhone 5机型iOS 10.3.4），因为高版本的是arm64架构后面可能会抽取失败。

下载完成之后你会得到一个 .ipsw 结尾的文件，其实它就是一个压缩包，直接使用Mac系统自带的 “归档实用工具” 打开即可，解压缩完成之后大概如下图：

![ipsw](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-170714-YD20200527-170657.png)

双击 **挂载中间数字最小的那个** dmg文件（别问为什么，其他两个你挂载试试），然后就能看到差不多就跟手机系统一样的文件结构了，如下图：

![system](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-171020-YD20200527-171012.png)

然后顺着 System/Library/Caches/com.apple.dyld/dyld_shared_cache_armX 找到动态共享缓存拷贝出来就可以了。

### 3.2 下载 dyld 动态链接器

苹果官方 [dyld 源码](https://opensource.apple.com/tarballs/dyld/) 是开源的，你可以[直接下载](https://opensource.apple.com/tarballs/dyld/dyld-733.6.tar.gz)下来。

解压缩之后找到 `launch-cache/dsc_extractor.cpp` ，如下图：

![dsc_extractor](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-171943-gFpwT5.png)

打开该文件，将文件最后的 `#if 0` 及这一行之前的代码全部删掉，然后再将文件末尾的 `#endif` 也删掉，最终只保留如下图的代码：

![code](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-172447-VwvSLm.png)

然后打开 **终端** ，进入到该路径下，执行：

```shell
clang++ dsc_extractor.cpp -o dsc_extractor
```

然后不出意外的话就会在当前目录下生成一个 **dsc_extractor** 命令行工具：

![cli](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-223954-dieTWi.jpg)

这就是用于抽取动态共享缓存库的工具了。

### 3.3 抽取动态库

到此，动态共享缓存库文件有了，抽取工具也制作好了，那么接下来就要开始抽取工作了。

将动态共享缓存库文件和命令行抽取工具放到同一个目录下，然后打开终端，进入该目录，执行下面的命令：

```shell
./dsc_extractor dyld_shared_cache_armv7s ./armv7s
```

就会开始抽取了，抽取完成之后会输出到 armv7s 文件夹中，如下图：

![armv7s](https://raw.githubusercontent.com/lishuzhi1121/oss/master/uPic/20200527-224340-rEgkbW.png)

可以看到系统公开的framework和私有的framework都抽取出来了，以后再想分析哪一个方法直接单独分析对应的framework就好了。

## 总结

动态共享缓存库的抽取比较简单，可能编译 **dsc_extractor** 工具时会遇到一些问题，不过基本通过谷歌都可以解决。

为了方便，我把自己抽取好的 **armv7s系统动态库** 放在了这里：

> 链接: [https://pan.baidu.com/s/1uN17igafUosN97B3lmyS3A](https://pan.baidu.com/s/1uN17igafUosN97B3lmyS3A)  密码: fia5

有需要的请自取！
