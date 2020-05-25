# main函数之前发生了什么？App冷启动过程？+load函数何时调用？

在我们新建的App项目代码中，XCode会自动创建一个`main.m`文件，其中定义了main函数:

![main-c](http://mweb.sandslee.com/2020-04-08-15863184022695.jpg)

> main函数的两个参数`(int argc, char *argv[])`，在iOS中其实是没有用到的，这两个参数主要是为了与标准ANSI C保持一致。第一个表示参数个数，一般是1，第二个表示参数内容，一般是该App的可执行文件的路径。

这里的 `main` 函数是我们整个App的入口，它的调用时机甚至会早于 `AppDelegate` 中的 `didFinishLaunching` 回调。

![didFinish-c](http://mweb.sandslee.com/2020-04-08-15863185456335.jpg)

因此我们经常会说， **main函数是我们App程序的入口函数** 。

## 1、main函数之前的调用栈

但是，事实上真的是这样吗？如果我们在XCode中设置符号断点 `_objc_init` :

![_objc_init-c](http://mweb.sandslee.com/2020-04-08-15863195718948.jpg)

则会发现，在进入main函数之前，其实系统还会调用 `void _objc_init(void)` 方法：

![void _objc_init(void)-c](http://mweb.sandslee.com/2020-04-08-15863194725202.jpg)

> 从图上可以看到，在 **_objc_init** 之前还有很多方法的调用，这个调用栈其实就是app冷启动到main函数之前的一系列操作了。

这个 `_objc_init` 函数，实际上是runtime的入口函数。

也就是说，在App的main函数之前，系统会首先对App的runtime运行环境做了一系列的初始化操作。那么问题来了， **这个 `_objc_init` 函数又是被谁调用的呢？？**

答案是苹果的 **动态链接器dyld(dynamic link editor)** 。dyld是一个操作系统级的组件，它会负责iOS系统中每个App启动时的环境初始化以及动态库加载到内存等一系列操作。

关于dyld我们不去多说，下面我们主要来看一下runtime中这个 `_objc_init` 函数：

```objc
// runtime源码: objc-os.mm中
/***********************************************************************
* _objc_init
* Bootstrap initialization. Registers our image notifier with dyld.
* Called by libSystem BEFORE library initialization time
**********************************************************************/
void _objc_init(void)
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    
    // fixme defer initialization until an objc-using image is found?
    environ_init();
    tls_init();
    static_init();
    runtime_init();
    exception_init();
    cache_init();
    _imp_implementationWithBlock_init();

    _dyld_objc_notify_register(&map_images, load_images, unmap_image);
}
```

除去上面一堆init函数，我们重点关注：

```objc
_dyld_objc_notify_register(&map_images, load_images, unmap_image);
```

`_dyld_objc_notify_register` 函数是 `dyld` 为runtime提供的一个钩子，用来注册对dyld中关于加载images的事件回调：

```objc
//
// Note: only for use by objc runtime
// Register handlers to be called when objc images are mapped, unmapped, and initialized.
// Dyld will call back the "mapped" function with an array of images that contain an objc-image-info section.
// Those images that are dylibs will have the ref-counts automatically bumped, so objc will no longer need to
// call dlopen() on them to keep them from being unloaded.  During the call to _dyld_objc_notify_register(),
// dyld will call the "mapped" function with already loaded objc images.  During any later dlopen() call,
// dyld will also call the "mapped" function.  Dyld will call the "init" function when dyld would be called
// initializers in that image.  This is when objc calls any +load methods in that image.
//
void _dyld_objc_notify_register(_dyld_objc_notify_mapped    mapped,
                                _dyld_objc_notify_init      init,
                                _dyld_objc_notify_unmapped  unmapped);
```

> 这个函数是dyld中的，函数定义详见：[苹果官方dyld源码](https://opensource.apple.com/source/dyld/dyld-732.8/include/mach-o/dyld_priv.h.auto.html)

根据注释可以知道，共注册了三个事件的回调：
1. _dyld_objc_notify_mapped：当OC的image（镜像）被加载映射到内存时
2. _dyld_objc_notify_init：当OC的image（镜像）被初始化时 **（+load方法会在此时被调用）**
3. _dyld_objc_notify_unmapped：当OC的image（镜像）被移除内存时

以上三个回调类型用的是函数指针，定义为：

``` objc
typedef void (*_dyld_objc_notify_mapped)(unsigned count, const char* const paths[], const struct mach_header* const mh[]);
typedef void (*_dyld_objc_notify_init)(const char* path, const struct mach_header* mh);
typedef void (*_dyld_objc_notify_unmapped)(const char* path, const struct mach_header* mh);
```

## 2、map_images 映射镜像

根据runtime中调用`_dyld_objc_notify_register`函数时传递的参数，可以知道当OC的image被dyld加载到内存后，会回调`_dyld_objc_notify_mapped`这个函数指针，实际上就是调用runtime中的`map_images`函数：

```objc
void
map_images(unsigned count, const char * const paths[],
           const struct mach_header * const mhdrs[])
{
    mutex_locker_t lock(runtimeLock);
    return map_images_nolock(count, paths, mhdrs);
}
```

`map_images`函数实质上会调用`map_images_nolock`函数（有删减）：

```objc
// runtime源码: objc-os.mm中
void 
map_images_nolock(unsigned mhCount, const char * const mhPaths[],
                  const struct mach_header * const mhdrs[])
{
    static bool firstTime = YES;
    header_info *hList[mhCount];
    uint32_t hCount;
    size_t selrefCount = 0;
    // Find all images with Objective-C metadata.
    hCount = 0;

    // Count classes. Size various table based on the total.
    int totalClasses = 0;
    int unoptimizedTotalClasses = 0;
    {
        uint32_t i = mhCount;
        while (i--) {
            const headerType *mhdr = (const headerType *)mhdrs[i];

            auto hi = addHeader(mhdr, mhPaths[i], totalClasses, unoptimizedTotalClasses);
            if (!hi) {
                // no objc data in this entry
                continue;
            }
            
            hList[hCount++] = hi;
        }
    }

    if (hCount > 0) {
        // 本质是这一句
        _read_images(hList, hCount, totalClasses, unoptimizedTotalClasses);
    }

    firstTime = NO;
    
    // Call image load funcs after everything is set up.
    for (auto func : loadImageFuncs) {
        for (uint32_t i = 0; i < mhCount; i++) {
            func(mhdrs[i]);
        }
    }
}
```

`map_images_nolock`函数很长，去除一些无关信息，可以看到它本质上是调用了`_read_images`函数， **其核心是用来读取Mach-O格式文件的runtime相关的section信息，并转化为runtime内部的数据结构** 。

我们来看一下 `_read_images` 函数的实现：

```objc
// runtime源码: objc-runtime-new.mm中
void _read_images(header_info **hList, uint32_t hCount, int totalClasses, int unoptimizedTotalClasses)
{
    header_info *hi;
    uint32_t hIndex;
    size_t count;
    size_t i;
    Class *resolvedFutureClasses = nil;
    size_t resolvedFutureClassCount = 0;
    static bool doneOnce;
    bool launchTime = NO;
    TimeLogger ts(PrintImageTimes);

    runtimeLock.assertLocked();

#define EACH_HEADER \
    hIndex = 0;         \
    hIndex < hCount && (hi = hList[hIndex]); \
    hIndex++

    if (!doneOnce) {
        doneOnce = YES;
        launchTime = YES;

#if SUPPORT_NONPOINTER_ISA
        // Disable non-pointer isa under some conditions.

# if SUPPORT_INDEXED_ISA
        // Disable nonpointer isa if any image contains old Swift code
        for (EACH_HEADER) {
            if (hi->info()->containsSwift()  &&
                hi->info()->swiftUnstableVersion() < objc_image_info::SwiftVersion3)
            {
                DisableNonpointerIsa = true;
                if (PrintRawIsa) {
                    _objc_inform("RAW ISA: disabling non-pointer isa because "
                                 "the app or a framework contains Swift code "
                                 "older than Swift 3.0");
                }
                break;
            }
        }
# endif

# if TARGET_OS_OSX
        // Disable non-pointer isa if the app is too old
        // (linked before OS X 10.11)
        if (dyld_get_program_sdk_version() < DYLD_MACOSX_VERSION_10_11) {
            DisableNonpointerIsa = true;
            if (PrintRawIsa) {
                _objc_inform("RAW ISA: disabling non-pointer isa because "
                             "the app is too old (SDK version " SDK_FORMAT ")",
                             FORMAT_SDK(dyld_get_program_sdk_version()));
            }
        }

        // Disable non-pointer isa if the app has a __DATA,__objc_rawisa section
        // New apps that load old extensions may need this.
        for (EACH_HEADER) {
            if (hi->mhdr()->filetype != MH_EXECUTE) continue;
            unsigned long size;
            if (getsectiondata(hi->mhdr(), "__DATA", "__objc_rawisa", &size)) {
                DisableNonpointerIsa = true;
                if (PrintRawIsa) {
                    _objc_inform("RAW ISA: disabling non-pointer isa because "
                                 "the app has a __DATA,__objc_rawisa section");
                }
            }
            break;  // assume only one MH_EXECUTE image
        }
# endif

#endif

        if (DisableTaggedPointers) {
            disableTaggedPointers();
        }
        
        initializeTaggedPointerObfuscator();

        if (PrintConnecting) {
            _objc_inform("CLASS: found %d classes during launch", totalClasses);
        }

        // namedClasses
        // Preoptimized classes don't go in this table.
        // 4/3 is NXMapTable's load factor
        int namedClassesSize = 
            (isPreoptimized() ? unoptimizedTotalClasses : totalClasses) * 4 / 3;
        gdb_objc_realized_classes =
            NXCreateMapTable(NXStrValueMapPrototype, namedClassesSize);

        ts.log("IMAGE TIMES: first time tasks");
    }

    // Fix up @selector references
    static size_t UnfixedSelectors;
    {
        mutex_locker_t lock(selLock);
        for (EACH_HEADER) {
            if (hi->hasPreoptimizedSelectors()) continue;

            bool isBundle = hi->isBundle();
            SEL *sels = _getObjc2SelectorRefs(hi, &count);
            UnfixedSelectors += count;
            for (i = 0; i < count; i++) {
                const char *name = sel_cname(sels[i]);
                SEL sel = sel_registerNameNoLock(name, isBundle);
                if (sels[i] != sel) {
                    sels[i] = sel;
                }
            }
        }
    }

    ts.log("IMAGE TIMES: fix up selector references");

    // Discover classes. Fix up unresolved future classes. Mark bundle classes.
    bool hasDyldRoots = dyld_shared_cache_some_image_overridden();

    for (EACH_HEADER) {
        if (! mustReadClasses(hi, hasDyldRoots)) {
            // Image is sufficiently optimized that we need not call readClass()
            continue;
        }

        classref_t const *classlist = _getObjc2ClassList(hi, &count);

        bool headerIsBundle = hi->isBundle();
        bool headerIsPreoptimized = hi->hasPreoptimizedClasses();

        for (i = 0; i < count; i++) {
            Class cls = (Class)classlist[i];
            Class newCls = readClass(cls, headerIsBundle, headerIsPreoptimized);

            if (newCls != cls  &&  newCls) {
                // Class was moved but not deleted. Currently this occurs 
                // only when the new class resolved a future class.
                // Non-lazily realize the class below.
                resolvedFutureClasses = (Class *)
                    realloc(resolvedFutureClasses, 
                            (resolvedFutureClassCount+1) * sizeof(Class));
                resolvedFutureClasses[resolvedFutureClassCount++] = newCls;
            }
        }
    }

    ts.log("IMAGE TIMES: discover classes");

    // Fix up remapped classes
    // Class list and nonlazy class list remain unremapped.
    // Class refs and super refs are remapped for message dispatching.
    
    if (!noClassesRemapped()) {
        for (EACH_HEADER) {
            Class *classrefs = _getObjc2ClassRefs(hi, &count);
            for (i = 0; i < count; i++) {
                remapClassRef(&classrefs[i]);
            }
            // fixme why doesn't test future1 catch the absence of this?
            classrefs = _getObjc2SuperRefs(hi, &count);
            for (i = 0; i < count; i++) {
                remapClassRef(&classrefs[i]);
            }
        }
    }

    ts.log("IMAGE TIMES: remap classes");

#if SUPPORT_FIXUP
    // Fix up old objc_msgSend_fixup call sites
    for (EACH_HEADER) {
        message_ref_t *refs = _getObjc2MessageRefs(hi, &count);
        if (count == 0) continue;

        if (PrintVtables) {
            _objc_inform("VTABLES: repairing %zu unsupported vtable dispatch "
                         "call sites in %s", count, hi->fname());
        }
        for (i = 0; i < count; i++) {
            fixupMessageRef(refs+i);
        }
    }

    ts.log("IMAGE TIMES: fix up objc_msgSend_fixup");
#endif

    bool cacheSupportsProtocolRoots = sharedCacheSupportsProtocolRoots();

    // Discover protocols. Fix up protocol refs.
    for (EACH_HEADER) {
        extern objc_class OBJC_CLASS_$_Protocol;
        Class cls = (Class)&OBJC_CLASS_$_Protocol;
        ASSERT(cls);
        NXMapTable *protocol_map = protocols();
        bool isPreoptimized = hi->hasPreoptimizedProtocols();

        // Skip reading protocols if this is an image from the shared cache
        // and we support roots
        // Note, after launch we do need to walk the protocol as the protocol
        // in the shared cache is marked with isCanonical() and that may not
        // be true if some non-shared cache binary was chosen as the canonical
        // definition
        if (launchTime && isPreoptimized && cacheSupportsProtocolRoots) {
            if (PrintProtocols) {
                _objc_inform("PROTOCOLS: Skipping reading protocols in image: %s",
                             hi->fname());
            }
            continue;
        }

        bool isBundle = hi->isBundle();

        protocol_t * const *protolist = _getObjc2ProtocolList(hi, &count);
        for (i = 0; i < count; i++) {
            readProtocol(protolist[i], cls, protocol_map, 
                         isPreoptimized, isBundle);
        }
    }

    ts.log("IMAGE TIMES: discover protocols");

    // Fix up @protocol references
    // Preoptimized images may have the right 
    // answer already but we don't know for sure.
    for (EACH_HEADER) {
        // At launch time, we know preoptimized image refs are pointing at the
        // shared cache definition of a protocol.  We can skip the check on
        // launch, but have to visit @protocol refs for shared cache images
        // loaded later.
        if (launchTime && cacheSupportsProtocolRoots && hi->isPreoptimized())
            continue;
        protocol_t **protolist = _getObjc2ProtocolRefs(hi, &count);
        for (i = 0; i < count; i++) {
            remapProtocolRef(&protolist[i]);
        }
    }

    ts.log("IMAGE TIMES: fix up @protocol references");

    // Discover categories.
    for (EACH_HEADER) {
        bool hasClassProperties = hi->info()->hasCategoryClassProperties();

        auto processCatlist = [&](category_t * const *catlist) {
            for (i = 0; i < count; i++) {
                category_t *cat = catlist[i];
                Class cls = remapClass(cat->cls);
                locstamped_category_t lc{cat, hi};
                
                if (!cls) {
                    // Category's target class is missing (probably weak-linked).
                    // Ignore the category.
                    if (PrintConnecting) {
                        _objc_inform("CLASS: IGNORING category \?\?\?(%s) %p with "
                                     "missing weak-linked target class",
                                     cat->name, cat);
                    }
                    continue;
                }
                
                // Process this category.
                if (cls->isStubClass()) {
                    // Stub classes are never realized. Stub classes
                    // don't know their metaclass until they're
                    // initialized, so we have to add categories with
                    // class methods or properties to the stub itself.
                    // methodizeClass() will find them and add them to
                    // the metaclass as appropriate.
                    if (cat->instanceMethods ||
                        cat->protocols ||
                        cat->instanceProperties ||
                        cat->classMethods ||
                        cat->protocols ||
                        (hasClassProperties && cat->_classProperties))
                    {
                        objc::unattachedCategories.addForClass(lc, cls);
                    }
                } else {
                    // First, register the category with its target class.
                    // Then, rebuild the class's method lists (etc) if
                    // the class is realized.
                    if (cat->instanceMethods ||  cat->protocols
                        ||  cat->instanceProperties)
                    {
                        if (cls->isRealized()) {
                            attachCategories(cls, &lc, 1, ATTACH_EXISTING);
                        } else {
                            objc::unattachedCategories.addForClass(lc, cls);
                        }
                    }
                    
                    if (cat->classMethods  ||  cat->protocols
                        ||  (hasClassProperties && cat->_classProperties))
                    {
                        if (cls->ISA()->isRealized()) {
                            attachCategories(cls->ISA(), &lc, 1, ATTACH_EXISTING | ATTACH_METACLASS);
                        } else {
                            objc::unattachedCategories.addForClass(lc, cls->ISA());
                        }
                    }
                }
            }
        };
        processCatlist(_getObjc2CategoryList(hi, &count));
        processCatlist(_getObjc2CategoryList2(hi, &count));
    }

    ts.log("IMAGE TIMES: discover categories");

    // Category discovery MUST BE Late to avoid potential races
    // when other threads call the new category code before
    // this thread finishes its fixups.

    // +load handled by prepare_load_methods()

    // Realize non-lazy classes (for +load methods and static instances)
    for (EACH_HEADER) {
        classref_t const *classlist = 
            _getObjc2NonlazyClassList(hi, &count);
        for (i = 0; i < count; i++) {
            Class cls = remapClass(classlist[i]);
            if (!cls) continue;

            addClassTableEntry(cls);

            if (cls->isSwiftStable()) {
                if (cls->swiftMetadataInitializer()) {
                    _objc_fatal("Swift class %s with a metadata initializer "
                                "is not allowed to be non-lazy",
                                cls->nameForLogging());
                }
                // fixme also disallow relocatable classes
                // We can't disallow all Swift classes because of
                // classes like Swift.__EmptyArrayStorage
            }
            realizeClassWithoutSwift(cls, nil);
        }
    }

    ts.log("IMAGE TIMES: realize non-lazy classes");

    // Realize newly-resolved future classes, in case CF manipulates them
    if (resolvedFutureClasses) {
        for (i = 0; i < resolvedFutureClassCount; i++) {
            Class cls = resolvedFutureClasses[i];
            if (cls->isSwiftStable()) {
                _objc_fatal("Swift class is not allowed to be future");
            }
            realizeClassWithoutSwift(cls, nil);
            cls->setInstancesRequireRawIsaRecursively(false/*inherited*/);
        }
        free(resolvedFutureClasses);
    }

    ts.log("IMAGE TIMES: realize future classes");

    if (DebugNonFragileIvars) {
        realizeAllClasses();
    }


    // Print preoptimization statistics
    if (PrintPreopt) {
        static unsigned int PreoptTotalMethodLists;
        static unsigned int PreoptOptimizedMethodLists;
        static unsigned int PreoptTotalClasses;
        static unsigned int PreoptOptimizedClasses;

        for (EACH_HEADER) {
            if (hi->hasPreoptimizedSelectors()) {
                _objc_inform("PREOPTIMIZATION: honoring preoptimized selectors "
                             "in %s", hi->fname());
            }
            else if (hi->info()->optimizedByDyld()) {
                _objc_inform("PREOPTIMIZATION: IGNORING preoptimized selectors "
                             "in %s", hi->fname());
            }

            classref_t const *classlist = _getObjc2ClassList(hi, &count);
            for (i = 0; i < count; i++) {
                Class cls = remapClass(classlist[i]);
                if (!cls) continue;

                PreoptTotalClasses++;
                if (hi->hasPreoptimizedClasses()) {
                    PreoptOptimizedClasses++;
                }
                
                const method_list_t *mlist;
                if ((mlist = ((class_ro_t *)cls->data())->baseMethods())) {
                    PreoptTotalMethodLists++;
                    if (mlist->isFixedUp()) {
                        PreoptOptimizedMethodLists++;
                    }
                }
                if ((mlist=((class_ro_t *)cls->ISA()->data())->baseMethods())) {
                    PreoptTotalMethodLists++;
                    if (mlist->isFixedUp()) {
                        PreoptOptimizedMethodLists++;
                    }
                }
            }
        }

        _objc_inform("PREOPTIMIZATION: %zu selector references not "
                     "pre-optimized", UnfixedSelectors);
        _objc_inform("PREOPTIMIZATION: %u/%u (%.3g%%) method lists pre-sorted",
                     PreoptOptimizedMethodLists, PreoptTotalMethodLists, 
                     PreoptTotalMethodLists
                     ? 100.0*PreoptOptimizedMethodLists/PreoptTotalMethodLists 
                     : 0.0);
        _objc_inform("PREOPTIMIZATION: %u/%u (%.3g%%) classes pre-registered",
                     PreoptOptimizedClasses, PreoptTotalClasses, 
                     PreoptTotalClasses 
                     ? 100.0*PreoptOptimizedClasses/PreoptTotalClasses
                     : 0.0);
        _objc_inform("PREOPTIMIZATION: %zu protocol references not "
                     "pre-optimized", UnfixedProtocolReferences);
    }

#undef EACH_HEADER
}
```

`_read_images` 函数又是写了很长，其实就是做了一件事，将`Mach-O文件`的section依次读取，并根据内容初始化runtime的内存结构。

根据注释，`_read_images` 函数主要做了下面这些事情：
1. 是否需要禁用isa优化。这里有三种情况：使用了swift 3.0前的swift代码。OSX版本早于10.11。在OSX系统下，Mach-O的DATA段明确指明了__objc_rawisa（不使用优化的isa）。
2. 在`__objc_classlist` section中读取class list。
3. 在`__objc_classrefs` section中读取class 引用的信息，并调用`remapClassRef`函数来处理。
4. 在`__objc_selrefs` section中读取selector的引用信息，并调用`sel_registerNameNoLock`函数处理。
5. 在`__objc_protolist` section中读取cls的Protocol信息，并调用`readProtocol`函数来读取Protocol信息。
6. 在`__objc_protorefs` section中读取protocol的引用信息，并调用`remapProtocolRef`函数来处理。
7. 在`__objc_nlclslist` section中读取non-lazy class信息，并调用`static Class realizeClass(Class cls)`函数来实现这些class。`realizeClass`函数核心是初始化objc_class数据结构，赋予初始值。
8. 在`__objc_catlist` section中读取category信息，并调用`addUnattachedCategoryForClass`函数来为类或元类添加对应的方法，属性和协议。

OK，以上就是在dyld将image map到内存后，runtime所做的事情： **根据Mach-O相关section中的信息，来初始化runtime的内存结构** 。

## 3、load_images 加载（初始化）镜像

当dyld要进行init image的时候，会回调`_dyld_objc_notify_init`函数。在runtime中, 是通过load_images方法做回调响应的：

```objc
// runtime源码: objc-runtime-new.mm中
void
load_images(const char *path __unused, const struct mach_header *mh)
{
    // Return without taking locks if there are no +load methods here.
    if (!hasLoadMethods((const headerType *)mh)) return;

    recursive_mutex_locker_t lock(loadMethodLock);

    // Discover load methods
    {
        mutex_locker_t lock2(runtimeLock);
        prepare_load_methods((const headerType *)mh);
    }

    // Call +load methods (without runtimeLock - re-entrant)
    call_load_methods();
}
```

这个函数内部比较简单，其实它也就只干了一件事儿： **调用Class的`+load`方法** 。分两步走：
1. Discover load methods
2. Call +load methods

### 3.1 Discover load methods

发现load方法的过程主要是调用`prepare_load_methods`函数：

```objc
// runtime源码: objc-runtime-new.mm中
void prepare_load_methods(const headerType *mhdr)
{
    size_t count, i;

    runtimeLock.assertLocked();
    // 先处理Class的load方法，核心是调用 `schedule_class_load` 函数
    classref_t const *classlist = 
        _getObjc2NonlazyClassList(mhdr, &count);
    for (i = 0; i < count; i++) {
        // 其内部会添加class到call +load队列
        schedule_class_load(remapClass(classlist[i]));
    }
    
    // 再处理Category的load方法
    category_t * const *categorylist = _getObjc2NonlazyCategoryList(mhdr, &count);
    for (i = 0; i < count; i++) {
        category_t *cat = categorylist[i];
        Class cls = remapClass(cat->cls);
        if (!cls) continue;  // category for ignored weak-linked class
        if (cls->isSwiftStable()) {
            _objc_fatal("Swift class extensions and categories on Swift "
                        "classes are not allowed to have +load methods");
        }
        realizeClassWithoutSwift(cls, nil);
        ASSERT(cls->ISA()->isRealized());
        // 添加category到call +load队列
        add_category_to_loadable_list(cat);
    }
}
```

`schedule_class_load` 函数的内部实现如下：

```objc
// runtime源码: objc-runtime-new.mm中
static void schedule_class_load(Class cls)
{
    if (!cls) return;
    ASSERT(cls->isRealized());  // _read_images should realize

    if (cls->data()->flags & RW_LOADED) return;

    // 递归调用：先把supperclass添加到call +load队列中
    schedule_class_load(cls->superclass);

    add_class_to_loadable_list(cls);
    cls->setInfo(RW_LOADED); 
}
```

这是一个递归调用，会先把superclass用`add_class_to_loadable_list`函数添加到 loadable class list 中：

```objc
// runtime源码: objc-loadmethod.mm中
void add_class_to_loadable_list(Class cls)
{
    IMP method;

    loadMethodLock.assertLocked();

    method = cls->getLoadMethod();
    if (!method) return;  // Don't bother if cls has no +load method
    
    if (PrintLoading) {
        _objc_inform("LOAD: class '%s' scheduled for +load", 
                     cls->nameForLogging());
    }
    
    if (loadable_classes_used == loadable_classes_allocated) {
        loadable_classes_allocated = loadable_classes_allocated*2 + 16;
        loadable_classes = (struct loadable_class *)
            realloc(loadable_classes,
                              loadable_classes_allocated *
                              sizeof(struct loadable_class));
    }
    
    loadable_classes[loadable_classes_used].cls = cls;
    loadable_classes[loadable_classes_used].method = method;
    loadable_classes_used++; // 下标++表示每次都是添加到队尾,相当于数组的append操作
}
```

从上面的分析可以看出，每一个定义了+load的类，都会被放到loadable_classes中。 **因此，+load方法并不存在子类重写父类之说。而且父类的+load方法会先于子类调用。**

我们再来看一下`add_category_to_loadable_list`函数：

```objc
// runtime源码: objc-loadmethod.mm中
void add_category_to_loadable_list(Category cat)
{
    IMP method;

    loadMethodLock.assertLocked();

    method = _category_getLoadMethod(cat);

    // Don't bother if cat has no +load method
    if (!method) return;

    if (PrintLoading) {
        _objc_inform("LOAD: category '%s(%s)' scheduled for +load", 
                     _category_getClassName(cat), _category_getName(cat));
    }
    
    if (loadable_categories_used == loadable_categories_allocated) {
        loadable_categories_allocated = loadable_categories_allocated*2 + 16;
        loadable_categories = (struct loadable_category *)
            realloc(loadable_categories,
                              loadable_categories_allocated *
                              sizeof(struct loadable_category));
    }

    loadable_categories[loadable_categories_used].cat = cat;
    loadable_categories[loadable_categories_used].method = method;
    loadable_categories_used++;
}
```

在`add_category_to_loadable_list`函数中，会将所有定义了+load方法的category都放到loadable_categories队列中。

### 3.2 Call +load methods

将定义了+load方法的class和category分别放到loadable_classes 和 loadable_categories 队列后，runtime就会依次读取队列中的class和category，并为之调用+load方法：

```objc
// runtime源码: objc-loadmethod.mm中
void call_load_methods(void)
{
    static bool loading = NO;
    bool more_categories;

    loadMethodLock.assertLocked();

    // Re-entrant calls do nothing; the outermost call will finish the job.
    if (loading) return;
    loading = YES;

    void *pool = objc_autoreleasePoolPush();

    do {
        // 1. Repeatedly call class +loads until there aren't any more
        while (loadable_classes_used > 0) {
            call_class_loads();
        }

        // 2. Call category +loads ONCE
        more_categories = call_category_loads();

        // 3. Run more +loads if there are classes OR more untried categories
    } while (loadable_classes_used > 0  ||  more_categories);

    objc_autoreleasePoolPop(pool);

    loading = NO;
}
```

从上面的`call_load_methods`方法可以看出：
1. class的+load方法调用时机是先于category的；
2. 结合前面class的+load方法添加到call +load队列中的顺序可以知道，superclass的+load方法调用时机是先于subclass的；
3. 没有继承关系的class的+load方法调用时机取决于编译顺序（Xcode中可以调整）；

## 4、unmap_image 卸载镜像

当dyld要将image移除内存时，会发送`_dyld_objc_notify_unmapped`通知。在runtime中，是用`unmap_image`方法来响应的：

```objc
// runtime源码: objc-runtime-new.mm中
void 
unmap_image(const char *path __unused, const struct mach_header *mh)
{
    recursive_mutex_locker_t lock(loadMethodLock);
    mutex_locker_t lock2(runtimeLock);
    unmap_image_nolock(mh);
}
```

其本质是调用`unmap_image_nolock`函数：

```objc
// runtime源码: objc-os.mm中
void 
unmap_image_nolock(const struct mach_header *mh)
{
    if (PrintImages) {
        _objc_inform("IMAGES: processing 1 newly-unmapped image...\n");
    }

    header_info *hi;
    
    // Find the runtime's header_info struct for the image
    for (hi = FirstHeader; hi != NULL; hi = hi->getNext()) {
        if (hi->mhdr() == (const headerType *)mh) {
            break;
        }
    }

    if (!hi) return;

    if (PrintImages) {
        _objc_inform("IMAGES: unloading image for %s%s%s\n", 
                     hi->fname(),
                     hi->mhdr()->filetype == MH_BUNDLE ? " (bundle)" : "",
                     hi->info()->isReplacement() ? " (replacement)" : "");
    }

    _unload_image(hi);

    // Remove header_info from header list
    removeHeader(hi);
    free(hi);
}
```

结合注释来看，这个函数主要是做了header信息的移除工作。

## 小结

到此，我们知道了dyld在main()函数之前，会调用runtime的`_objc_init`函数。`_objc_init`是runtime的入口函数，它会根据Mach-O文件中相关的section信息来初始化runtime内存空间。比如，加载class，protocol，以及附加category到class，调用+load方法等。

当dyld将我们App的运行环境都准备好后，dyld 会清理现场，将调用栈回归，调用main()函数，这时候，我们的App就算启动了。

在main()函数被调用前，系统其实已经为我们做了很多的准备工作，大致概括：

> * 加载可执行文件。（App里的所有.o文件）
> * 加载动态链接库，进行rebase指针调整和bind符号绑定。 （xcode run加个参数可以看库加载时间:DYLD_PRINT_STATISTICS=1）
> * ObjC的runtime初始化。 包括：ObjC相关Class的注册、category注册、selector唯一性检查等
> * 初始化。 包括：执行+load()方法、用attribute((constructor))修饰的函数的调用、创建C++静态全局变量

附：
查看app启动时的Total pre-main time：

![pre-main-c](http://mweb.sandslee.com/2020-04-08-15863402449342.jpg)

> 孤独的 main 函数，看上去是程序的开始，却是一段精彩的终结。