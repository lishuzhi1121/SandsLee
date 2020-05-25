## 1、iOS对象是如何初始化的？`+ alloc` `- init` 做了什么？

iOS对象的初始化过程其实只是为 **一个分配内存空间，并且初始化 isa_t 结构体** 的过程。

### \+ alloc 方法的实现:

```objc
// runtime源码: NSObject.mm 中
+ (id)alloc {
    return _objc_rootAlloc(self);
}
```

alloc 方法的实现真的是非常的简单, 它直接调用了另一个私有方法 id _objc_rootAlloc(Class cls):

```objc
// runtime源码: NSObject.mm 中
// Base class implementation of +alloc. cls is not nil.
// Calls [cls allocWithZone:nil].
id
_objc_rootAlloc(Class cls)
{
    return callAlloc(cls, false/*checkNil*/, true/*allocWithZone*/);
}
```

这个 `_objc_rootAlloc` 方法内部直接调用了 `callAlloc` 方法，下面就是上帝类 NSObject 对 callAlloc 的实现，我们省略了非常多的代码，展示了最常见的执行路径：

```objc
// runtime源码: NSObject.mm 中
// Call [cls alloc] or [cls allocWithZone:nil], with appropriate 
// shortcutting optimizations.
static ALWAYS_INLINE id
callAlloc(Class cls, bool checkNil, bool allocWithZone=false)
{
#if __OBJC2__
    if (slowpath(checkNil && !cls)) return nil;
    if (fastpath(!cls->ISA()->hasCustomAWZ())) {
        return _objc_rootAllocWithZone(cls, nil); // 核心在这个方法
    }
#endif

    // No shortcuts available.
    if (allocWithZone) {
        return ((id(*)(id, SEL, struct _NSZone *))objc_msgSend)(cls, @selector(allocWithZone:), nil);
    }
    return ((id(*)(id, SEL))objc_msgSend)(cls, @selector(alloc));
}
```

这个 `callAlloc` 方法实现也很简单，主要就是调用了 `_objc_rootAllocWithZone` 方法：

```objc
// runtime源码: objc-runtime-new.mm 中
id
_objc_rootAllocWithZone(Class cls, malloc_zone_t *zone __unused)
{
    // allocWithZone under __OBJC2__ ignores the zone parameter
    return _class_createInstanceFromZone(cls, 0, nil,
                                         OBJECT_CONSTRUCT_CALL_BADALLOC);
}
```

对象初始化中最重要的操作都在 `_class_createInstanceFromZone` 方法中执行：

```objc
// runtime源码: objc-runtime-new.mm 中
static ALWAYS_INLINE id
_class_createInstanceFromZone(Class cls, size_t extraBytes, void *zone,
                              int construct_flags = OBJECT_CONSTRUCT_NONE,
                              bool cxxConstruct = true,
                              size_t *outAllocatedSize = nil)
{
    ASSERT(cls->isRealized());

    // Read class's info bits all at once for performance
    bool hasCxxCtor = cxxConstruct && cls->hasCxxCtor();
    bool hasCxxDtor = cls->hasCxxDtor();
    bool fast = cls->canAllocNonpointer();
    size_t size;

    size = cls->instanceSize(extraBytes); // 调用calloc申请内存之前需要先获得对象在内存中的大小
    if (outAllocatedSize) *outAllocatedSize = size;

    id obj;
    // 申请内存
    if (zone) {
        obj = (id)malloc_zone_calloc((malloc_zone_t *)zone, 1, size);
    } else {
        obj = (id)calloc(1, size);
    }
    if (slowpath(!obj)) {
        if (construct_flags & OBJECT_CONSTRUCT_CALL_BADALLOC) {
            return _objc_callBadAllocHandler(cls);
        }
        return nil;
    }

    if (!zone && fast) {
        obj->initInstanceIsa(cls, hasCxxDtor); // 初始化isa指针
    } else {
        // Use raw pointer isa on the assumption that they might be
        // doing something weird with the zone or RR.
        obj->initIsa(cls);
    }

    if (fastpath(!hasCxxCtor)) {
        return obj;
    }

    construct_flags |= OBJECT_CONSTRUCT_FREE_ONFAILURE;
    return object_cxxConstructFromClass(obj, cls, construct_flags); // 组织对象结构并返回
}
```
获取对象大小相关方法调用如下：

```objc
// runtime源码: objc-runtime-new.mm 中
// May be unaligned depending on class's ivars.
uint32_t unalignedInstanceSize() const {
    ASSERT(isRealized());
    return data()->ro->instanceSize;
}

// Class's ivar size rounded up to a pointer-size boundary.
uint32_t alignedInstanceSize() const {
    return word_align(unalignedInstanceSize());
}

size_t instanceSize(size_t extraBytes) const {
    if (fastpath(cache.hasFastInstanceSize(extraBytes))) {
        return cache.fastInstanceSize(extraBytes);
    }

    size_t size = alignedInstanceSize() + extraBytes;
    // CF requires all objects be at least 16 bytes.
    if (size < 16) size = 16;
    return size;
}
```

实例大小 instanceSize 会存储在类的 isa_t 结构体中，然后经过对齐最后返回。

> Core Foundation 需要所有的对象的大小都必须大于或等于 16 字节。

在对象的初始化过程中除了使用 `calloc` 来分配内存之外，还需要根据类 **初始化 isa_t 结构体** ：

```objc
// runtime源码: objc-object.h 中
inline void 
objc_object::initInstanceIsa(Class cls, bool hasCxxDtor)
{
    ASSERT(!cls->instancesRequireRawIsa());
    ASSERT(hasCxxDtor == cls->hasCxxDtor());

    initIsa(cls, true, hasCxxDtor);
}

inline void 
objc_object::initIsa(Class cls, bool nonpointer, bool hasCxxDtor) 
{ 
    ASSERT(!isTaggedPointer()); 
    
    if (!nonpointer) {
        isa = isa_t((uintptr_t)cls);
    } else {
        ASSERT(!DisableNonpointerIsa);
        ASSERT(!cls->instancesRequireRawIsa());

        isa_t newisa(0);

#if SUPPORT_INDEXED_ISA
        ASSERT(cls->classArrayIndex() > 0);
        newisa.bits = ISA_INDEX_MAGIC_VALUE;
        // isa.magic is part of ISA_MAGIC_VALUE
        // isa.nonpointer is part of ISA_MAGIC_VALUE
        newisa.has_cxx_dtor = hasCxxDtor;
        newisa.indexcls = (uintptr_t)cls->classArrayIndex();
#else
        newisa.bits = ISA_MAGIC_VALUE;
        // isa.magic is part of ISA_MAGIC_VALUE
        // isa.nonpointer is part of ISA_MAGIC_VALUE
        newisa.has_cxx_dtor = hasCxxDtor;
        newisa.shiftcls = (uintptr_t)cls >> 3;
#endif

        // This write must be performed in a single store in some cases
        // (for example when realizing a class because other threads
        // may simultaneously try to use the class).
        // fixme use atomics here to guarantee single-store and to
        // guarantee memory order w.r.t. the class index table
        // ...but not too atomic because we don't want to hurt instantiation
        isa = newisa;
    }
}
```

isa结构体的初始化就只是简单的构建isa_t的数据结构，完成对象内存大小计算、内存申请、isa指针初始化之后就基本完成了一个对象的初始化，接下来调用 `object_cxxConstructFromClass` 方法组织对象结构并返回：

```objc
// runtime源码: objc-class.mm 中
id 
object_cxxConstructFromClass(id obj, Class cls, int flags)
{
    ASSERT(cls->hasCxxCtor());  // required for performance, not correctness

    id (*ctor)(id);
    Class supercls;

    supercls = cls->superclass;

    // Call superclasses' ctors first, if any.
    if (supercls  &&  supercls->hasCxxCtor()) {
        bool ok = object_cxxConstructFromClass(obj, supercls, flags);
        if (slowpath(!ok)) return nil;  // some superclass's ctor failed - give up
    }

    // 重要：查找方法并载入缓存!!!
    ctor = (id(*)(id))lookupMethodInClassAndLoadCache(cls, SEL_cxx_construct);
    if (ctor == (id(*)(id))_objc_msgForward_impcache) return obj;  // no ctor - ok
    
    // Call this class's ctor.
    if (PrintCxxCtors) {
        _objc_inform("CXX: calling C++ constructors for class %s", 
                     cls->nameForLogging());
    }
    if (fastpath((*ctor)(obj))) return obj;  // ctor called and succeeded - ok

    supercls = cls->superclass; // this reload avoids a spill on the stack

    // This class's ctor was called and failed.
    // Call superclasses's dtors to clean up.
    if (supercls) object_cxxDestructFromClass(obj, supercls);
    if (flags & OBJECT_CONSTRUCT_FREE_ONFAILURE) free(obj);
    if (flags & OBJECT_CONSTRUCT_CALL_BADALLOC) {
        return _objc_callBadAllocHandler(cls);
    }
    return nil;
}
```

至此，一个对象的初始化就完成了，这个对象就可以使用了。

所以，其实我们 **创建对象的时候就只要调用 `+ alloc` 方法就行了** ，那么 `- init` 方法又干了啥呢？我们为什么还要调用 `- init` 方法呢？？

### \- init 方法的实现

```objc
// runtime源码: NSObject.mm 中
- (id)init {
    return _objc_rootInit(self);
}
```

其中就直接调用了 `_objc_rootInit` 方法:

```objc
// runtime源码: NSObject.mm 中
id
_objc_rootInit(id obj)
{
    // In practice, it will be hard to rely on this function.
    // Many classes do not properly chain -init calls.
    return obj;
}
```

`_objc_rootInit` 竟然直接返回了 obj，里面什么也没有做！！那么这个init还拿来干嘛呢？这就是抽象工厂设计模式。

我们自定义一个对象，定义它的属性，在它初始化的时候，它就有一些默认值，比如，占位图、占位字符等，此时我们就需要重写init方法，在它的初始化方法中，给这些属性赋值。OK，这就是init的作用！！


```objc
- (instancetype)init {
    if (self = [super init]) {
        // 为自定义属性赋值
        
    }
    return self;
}
```
