## 2、iOS运行时消息转发机制/转发流程？

当我们用中括号`[]`调用OC方法的时候，实际上会进入 **消息发送** 和 **消息转发** 流程：

> 消息发送（Messaging），runtime系统会根据SEL查找对应的IMP，查找到，则调用函数指针进行方法调用；查找不到，则进入动态消息解析和转发流程，如果动态解析和消息转发失败，则程序crash（undefined selector...）并记录日志。

首先，思考两个问题：

* 1. 类实例可以调用类方法吗？类实例可以调用对象方法吗？为什么？
* 2. 下面的代码输出什么？


```objc
@interface Father : NSObject
@end

@implementation Father
@end

@interface Son : Father
- (void)showClass;
@end

@implementation Son
- (void)showClass {
    NSLog(@"self class = %@, super class = %@", [self class], [super class]);
}

...
Son *son = [Son new];
[son showClass];    // 这里输出什么??
...
```

OK，且看且思考，继续！

### 消息相关的数据结构

#### SEL

SEL被称之为方法选择器（或方法编号），它相当于一个Key，在类的消息列表中，可以根据这个Key来找到对应的消息实现。
在runtime中，SEL的定义如下：

```objc
// runtime源码: objc.h 中
/// An opaque type that represents a method selector.
typedef struct objc_selector *SEL;
```

它是一个不透明的定义，似乎苹果故意隐藏了它的实现。目前 **SEL仅是一个字符串** 。

这里要注意，即使消息的参数类型不同(不是参数数量)或方法所属的类也不同，但只要方法名相同，SEL也是一样的。所以，**SEL并不能单独作为唯一的Key，必须结合消息发送的目标Class，才能找到最终的IMP。**

> 我们可以通过OC编译器命令@selector()或runtime函数sel_registerName，来获取一个SEL类型的方法选择器。

#### method_t

当需要发送消息的时候，runtime会在Class的方法列表中寻找方法的实现。方法列表中的方法是以结构体 `method_t` 存储的。

```objc
// runtime源码: objc-runtime-new.mm 中
struct method_t {
    SEL name;
    const char *types;
    MethodListIMP imp;

    struct SortBySELAddress :
        public std::binary_function<const method_t&,
                                    const method_t&, bool>
    {
        bool operator() (const method_t& lhs,
                         const method_t& rhs)
        { return lhs.name < rhs.name; }
    };
};
```

可以看到method_t包含一个`SEL`作为Key，同时有一个指向函数实现的指针`MethodListIMP`，这个其实就是`IMP`，只是换了个名字：

```objc
// runtime源码: objc-ptrauth.h 中
#if __has_feature(ptrauth_calls)
// Method lists use process-independent signature for compatibility.
using MethodListIMP = IMP __ptrauth_objc_method_list_imp;
#else
using MethodListIMP = IMP;
#endif
```

method_t还包含一个属性`const char *types;`，types是一个C字符串，用于表明方法的返回值和参数类型。一般是这种格式的：

```objc
/**
v 表示返回值类型为void
@ 固定的,表示方法接收者,id
: 固定的,表示SEL方法
@ 可选,表示参数类型,方法有参数时使用,@表示参数为id
*/
v@:@
```

#### IMP

IMP实际是一个函数指针，用于实际的方法调用。在runtime中定义是这样的：

```objc
// runtime源码: objc.h 中
/// A pointer to the function of a method implementation. 
#if !OBJC_OLD_DISPATCH_PROTOTYPES
typedef void (*IMP)(void /* id, SEL, ... */ ); 
#else
typedef id _Nullable (*IMP)(id _Nonnull, SEL _Nonnull, ...); 
#endif
```

> IMP其实是由编译器生成的，如果我们知道了IMP的地址，则可以绕过runtime的消息发送过程，直接调用函数实现的。

在消息发送的过程中，runtime就是根据id和SEL来唯一确定IMP并调用之的。

### 消息发送

当我们用`[]`调用OC对象方法时，实际上是调用runtime的`objc_msgSend`函数向OC对象发送消息，其定义如下：

```objc
// runtime源码: message.h 中
OBJC_EXPORT id _Nullable
objc_msgSend(id _Nullable self, SEL _Nonnull op, ...)
    OBJC_AVAILABLE(10.0, 2.0, 9.0, 1.0, 2.0);
```

> 从runtime源码的注释中可以看到，当进行方法调用时，编译器会根据实际情况，将消息发送改为下面4个函数之一：
> * objc_msgSend
> * objc_msgSend_stret
> * objc_msgSendSuper
> * objc_msgSendSuper_stret

> 当我们将消息发送给super class的时候，编译器会将消息发送改写为 `SendSuper` 的格式，例如调用[super viewDidLoad]，会被编译器改写为 `objc_msgSendSuper` 的形式。

**至于以`_stret`结尾的msgSend方法，只是表明方法返回值是一个结构体类型而已。**

#### objc_msgSend

objc_msgSend 的伪代码实现如下:

```objc
id objc_msgSend(id self, SEL cmd, ...) {
	if(self == nil)
		return 0;
	Class cls = objc_getClass(self);
	IMP imp = class_getMethodImplementation(cls, cmd);
	return imp?imp(self, cmd, ...):0;
}
```

> 在runtime源码中，objc_msgSend方法其实是用汇编写的。为什么用汇编？一是因为objc_msgSend的返回值类型是可变的，需要用到汇编的特性；二是因为汇编可以提高代码的效率。

对应arm64，其汇编源码是这样的（有所删减）：

```objc
// runtime源码: objc-msg-arm64.s 中
	ENTRY _objc_msgSend
	UNWIND _objc_msgSend, NoFrame

	cmp	p0, #0			// nil check and tagged pointer check
#if SUPPORT_TAGGED_POINTERS
	b.le	LNilOrTagged		//  (MSB tagged pointer looks negative)
#else
	b.eq	LReturnZero
#endif
	ldr	p13, [x0]		// p13 = isa
	GetClassFromIsa_p16 p13		// p16 = class
LGetIsaDone:
	// calls imp or objc_msgSend_uncached
	CacheLookup NORMAL, _objc_msgSend

#if SUPPORT_TAGGED_POINTERS
LNilOrTagged:
	b.eq	LReturnZero		// nil check
```

稍微看的懂一点汇编，再结合注释，大体意思如下：

1. 通过 `cmp	p0, #0` 检查receiver是否为nil，如果为nil则进入`LNilOrTagged`，返回 0；
2. 如果不为nil，则通过`ldr	p13, [x0]`将receiver的`isa`存入`p13`寄存器；
3. 通过 `GetClassFromIsa_p16 p13` 从`p13`寄存器中取出`isa`中的`class`，放到`p16`寄存器中；
4. 调用`CacheLookup`，在这个函数中，会先查找`class`的cache，根据注释，如果命中则调用对应的imp，如果未命中，则进入`objc_msgSend_uncached`；

objc_msgSend_uncached 在runtime中也是汇编，实现如下：

```objc
// runtime源码: objc-msg-arm.s 中
	STATIC_ENTRY __objc_msgSend_uncached

	// THIS IS NOT A CALLABLE C FUNCTION
	// Out-of-band r9 is the class to search
	
	MethodTableLookup NORMAL	// returns IMP in r12
	bx	r12

	END_ENTRY __objc_msgSend_uncached
```

这个段汇编比较简单，其内部直接调用了`MethodTableLookup`，它其实是一个汇编的宏定义，具体实现如下：

```objc
// runtime源码: objc-msg-arm.s 中
/////////////////////////////////////////////////////////////////////
//
// MethodTableLookup	NORMAL|STRET
//
// Locate the implementation for a selector in a class's method lists.
//
// Takes: 
//	  $0 = NORMAL, STRET
//	  r0 or r1 (STRET) = receiver
//	  r1 or r2 (STRET) = selector
//	  r9 = class to search in
//
// On exit: IMP in r12, eq/ne set for forwarding
//
/////////////////////////////////////////////////////////////////////
	
.macro MethodTableLookup
	
	stmfd	sp!, {r0-r3,r7,lr}
	add	r7, sp, #16
	sub	sp, #8			// align stack
	FP_SAVE

	// lookUpImpOrForward(obj, sel, cls, LOOKUP_INITIALIZE | LOOKUP_RESOLVER)
.if $0 == NORMAL
	// receiver already in r0
	// selector already in r1
.else
	mov 	r0, r1			// receiver
	mov 	r1, r2			// selector
.endif
	mov	r2, r9			// class to search
	mov	r3, #3			// LOOKUP_INITIALIZE | LOOKUP_INITIALIZE
	blx	_lookUpImpOrForward    // 注意：这里调用了一个C的函数！！！
	mov	r12, r0			// r12 = IMP
	
.if $0 == NORMAL
	cmp	r12, r12		// set eq for nonstret forwarding
.else
	tst	r12, r12		// set ne for stret forwarding
.endif

	FP_RESTORE
	add	sp, #8			// align stack
	ldmfd	sp!, {r0-r3,r7,lr}

.endmacro
```

其内部执行了`blx _lookUpImpOrForward`指令，其实是调用了一个C函数`IMP lookUpImpOrForward(id inst, SEL sel, Class cls, int behavior)`。

`lookUpImpOrForward`函数的目的在于根据class和SEL，在class或其super class中找到并返回对应的实现IMP，**同时，cache所找到的IMP到当前class的消息列表中**。如果没有找到对应的IMP，`lookUpImpOrForward`就会进入消息转发流程。

`lookUpImpOrForward` 的实现如下（有所删减）：

```objc
// runtime源码: objc-runtime-new.mm 中
IMP lookUpImpOrForward(id inst, SEL sel, Class cls, int behavior)
{
    const IMP forward_imp = (IMP)_objc_msgForward_impcache;
    IMP imp = nil;
    Class curClass;

    runtimeLock.assertUnlocked();

    // 先在class的cache中查找imp
    if (fastpath(behavior & LOOKUP_CACHE)) {
        imp = cache_getImp(cls, sel);
        if (imp) goto done_nolock;
    }

    runtimeLock.lock();
    checkIsKnownClass(cls);

    if (slowpath(!cls->isRealized())) {
        cls = realizeClassMaybeSwiftAndLeaveLocked(cls, runtimeLock);
        // runtimeLock may have been dropped but is now locked again
    }

    if (slowpath((behavior & LOOKUP_INITIALIZE) && !cls->isInitialized())) {
        cls = initializeAndLeaveLocked(cls, inst, runtimeLock);
    }

    runtimeLock.assertLocked();
    curClass = cls;

    for (unsigned attempts = unreasonableClassCount();;) {
        // 然后在当前class的method list中查找imp.
        Method meth = getMethodNoSuper_nolock(curClass, sel);
        if (meth) {
            imp = meth->imp;
            goto done;
        }

        if (slowpath((curClass = curClass->superclass) == nil)) {
            // 当从子类一直找到父类都没有找到，并且没有实现动态方法解析
            // 则进入消息转发流程
            imp = forward_imp;
            break;
        }

        // 接着查找父类方法缓存
        imp = cache_getImp(curClass, sel);
        if (slowpath(imp == forward_imp)) {
            // 如果查找到的父类方法是进行消息转发时，则停止查找，但并不缓存到子类中
            // 此时优先调用子类的动态方法解析
            break;
        }
        if (fastpath(imp)) {
            // 找到父类的方法并缓存到子类中
            goto done;
        }
    }

    // No implementation found. Try method resolver once.
    // 子类和所有父类中都未找到对应的IMP或父类中找到的imp是进行消息转发，则先尝试一次动态方法解析
    // 这也就是为什么我们可以使用 `resolveClassMethod`或`resolveInstanceMethod`进行方法补救的原因
    if (slowpath(behavior & LOOKUP_RESOLVER)) {
        behavior ^= LOOKUP_RESOLVER;
        return resolveMethod_locked(inst, sel, cls, behavior);
    }

 done:
    // 找到对应的IMP，则进行消息发送并cache结果到当前class
    log_and_fill_cache(cls, imp, sel, inst, curClass);
    runtimeLock.unlock();
 done_nolock:
    if (slowpath((behavior & LOOKUP_NIL) && imp == forward_imp)) {
        return nil;
    }
    return imp;
}
```

上面这段代码比较长，好在逻辑清晰，到此我们就可以很清晰的知晓runtime的消息处理流程了：
> 1. 尝试在当前receiver对应的Class的方法缓存method cache中查找imp；
> 2. 尝试在当前receiver对应的Class的方法列表method list中查找imp；
> 3. 尝试在当前receiver对应的Class的所有父类super classes中查找（同样是先找cache，再找list）;
> 4. 如果当前class和所有父类super classes中都未找到对应的imp或父类中找到的imp是进行消息转发，则先尝试一次动态方法解析；
> 5. 动态方法解析失败，则进行消息转发；

另外，在查找class的方法列表中是否有SEL对应的IMP实现时，是调用函数`getMethodNoSuper_nolock`：

```objc
// runtime源码: objc-runtime-new.mm 中
static method_t *
getMethodNoSuper_nolock(Class cls, SEL sel)
{
    runtimeLock.assertLocked();

    ASSERT(cls->isRealized());

    for (auto mlists = cls->data()->methods.beginLists(), 
              end = cls->data()->methods.endLists(); 
         mlists != end;
         ++mlists)
    {
        method_t *m = search_method_list_inline(*mlists, sel);
        if (m) return m;
    }

    return nil;
}
```

其内部是直接调用了`search_method_list_inline`函数，具体实现如下：

```objc
// runtime源码: objc-runtime-new.mm 中
ALWAYS_INLINE static method_t *
search_method_list_inline(const method_list_t *mlist, SEL sel)
{
    int methodListIsFixedUp = mlist->isFixedUp();
    int methodListHasExpectedSize = mlist->entsize() == sizeof(method_t);
    // 如果已经是排序好的method list则采用快速查找调用`findMethodInSortedMethodList`函数
    if (fastpath(methodListIsFixedUp && methodListHasExpectedSize)) {
        return findMethodInSortedMethodList(sel, mlist);
    } else {
        // 对于未排序的 method list 采用遍历进行线性查找
        for (auto& meth : *mlist) {
            if (meth.name == sel) return &meth;
        }
    }

    return nil;
}
```

这个函数也是比较简单的，如果方法列表已排序，则直接调用`findMethodInSortedMethodList`函数：

```objc
// runtime源码: objc-runtime-new.mm 中
ALWAYS_INLINE static method_t *
findMethodInSortedMethodList(SEL key, const method_list_t *list) // 注意到这里其实就可以看出来SEL本质就是一个用于查找imp的key了
{
    ASSERT(list);
    const method_t * const first = &list->first;
    const method_t *base = first;
    const method_t *probe;
    uintptr_t keyValue = (uintptr_t)key;
    uint32_t count;
    
    for (count = list->count; count != 0; count >>= 1) {
        probe = base + (count >> 1);
        uintptr_t probeValue = (uintptr_t)probe->name;
        if (keyValue == probeValue) {
            // 这里再次查找主要是为了找到第一个IMP，主要是为了分类中的方法能够实现“覆盖”
            while (probe > first && keyValue == (uintptr_t)probe[-1].name) {
                probe--;
            }
            return (method_t *)probe;
        }
        if (keyValue > probeValue) {
            base = probe + 1;
            count--;
        }
    }
    return nil;
}
```

直到这里，runtime的方法查找过程就比较明确了，包括通过SEL是如何找到IMP的都有了明确的解释。

> 这里顺便说一下Category覆盖类原始方法的问题，由于在methods中是线性查找的，会返回第一个和SEL匹配的imp。而在class的realizeClass方法中，会调用methodizeClass来初始化class的方法列表。在methodizeClass方法中，会将Category方法插入到class方法列表头部，所以，在runtime寻找SEL的对应IMP实现时，会先找到Category中定义的imp返回，从而实现了原始方法覆盖的效果。

关于方法查找过程，总结两点：
1. 实例方法的消息查找流程：通过类对象实例的isa指针查找到对象的class，进行查找；
2. 类方法的消息查找流程: 通过类的isa指针找到类对应的元类, 沿着元类的super class链一路查找；

#### objc_msgSendSuper

看完了objc_msgSend方法的调用流程，我们再来看一下objc_msgSendSuper是如何调用的。当我们在代码里面显示的调用`super`方法时，runtime就会调用objc_msgSendSuper来完成消息发送。

`objc_msgSendSuper` 的定义如下：

```objc
// runtime源码: message.h 中
OBJC_EXPORT id _Nullable
objc_msgSendSuper(struct objc_super * _Nonnull super, SEL _Nonnull op, ...)
    OBJC_AVAILABLE(10.0, 2.0, 9.0, 1.0, 2.0);
```

可以看到，调用super方法时，`msgSendSuper`的第一个参数不再是`id self`，而是一个`objc_super *` 。`objc_super`定义如下(有删减)：

```objc
// runtime源码: message.h 中
/// Specifies the superclass of an instance. 
struct objc_super {
    /// Specifies an instance of a class.
    __unsafe_unretained _Nonnull id receiver;
    __unsafe_unretained _Nonnull Class super_class;
};
```

`objc_super`包含两个数据：
* **receiver**：表明要将消息发送给谁。它应该是我们的类实例（注意，是当前类实例，而不是super）；
* **super_class**：表明要到哪里去寻找SEL所对应的IMP。它应该是我们类实例所对应类的super class。（即要直接到super class中寻找IMP，而略过当前class的method list）；

简单来说， **当调用super method时，runtime会到super class中找到IMP，然后发送到当前class的实例上。** 因此，虽然IMP的实现是用的super class，但是，最终作用对象，仍然是当前class 的实例。这也就是为什么

```objc
NSLog(@"self class = %@, super class = %@", [self class], [super class]);
```

会输出同样的内容，即`[self class]`的内容。

`objc_msgSendSuper` 的汇编实现如下：

```objc
// runtime源码: objc-msg-arm.s 中
	ENTRY _objc_msgSendSuper
	
	ldr	r9, [r0, #CLASS]	// r9 = struct super->class 注意:r9寄存器存的是super class用于方法查找
	CacheLookup NORMAL, _objc_msgSendSuper
	// cache hit, IMP in r12, eq already set for nonstret forwarding
	ldr	r0, [r0, #RECEIVER]	// load real receiver 注意:但是真正的接收者还是当前对象
	bx	r12			// call imp

	CacheLookup2 NORMAL, _objc_msgSendSuper
	// cache miss
	ldr	r9, [r0, #CLASS]	// r9 = struct super->class
	ldr	r0, [r0, #RECEIVER]	// load real receiver
	b	__objc_msgSend_uncached
	
	END_ENTRY _objc_msgSendSuper
```

可以看到，它就是在struct super->class的method list 中寻找对应的IMP，而real receiver则是super->receiver，即当前类实例。

如果在super class的cache中没有找到IMP的话，则同样会调用`__objc_msgSend_uncached`，这和`objc_msgSend`是一样的，最终都会调用到`lookUpImpOrForward`，只不过，这里传入`lookUpImpOrForward`里面的cls，使用了super class而已。

至此，整个消息发送涉及到的函数以及方法查找过程就说完了，那么在方法查找的过程中，我们说到：**如果在当前class和所有父类super classes中都未找到对应的imp或父类中找到的imp是进行消息转发，则先尝试一次动态方法解析**。

### 动态方法解析 Method Resolver

如果在类的继承体系中，没有找到相应的IMP，runtime首先会进行消息的动态解析。所谓动态解析，就是**给我们一个机会，将方法实现在运行时动态的添加到当前的类中**。然后，runtime会重新尝试走一遍消息查找的过程：

```objc
// 节选自上方 lookUpImpOrForward 方法
// No implementation found. Try method resolver once.
if (slowpath(behavior & LOOKUP_RESOLVER)) {
    behavior ^= LOOKUP_RESOLVER;
    return resolveMethod_locked(inst, sel, cls, behavior);
}
```

进行动态方法解析时，主要调用`resolveMethod_locked`函数：

```objc
// runtime源码: objc-runtime-new.mm 中
static NEVER_INLINE IMP
resolveMethod_locked(id inst, SEL sel, Class cls, int behavior)
{
    runtimeLock.assertLocked();
    ASSERT(cls->isRealized());

    runtimeLock.unlock();

    if (! cls->isMetaClass()) {
        // try [cls resolveInstanceMethod:sel]
        resolveInstanceMethod(inst, sel, cls);
    } 
    else {
        // try [nonMetaClass resolveClassMethod:sel]
        // and [cls resolveInstanceMethod:sel]
        resolveClassMethod(inst, sel, cls);
        if (!lookUpImpOrNil(inst, sel, cls)) {
            resolveInstanceMethod(inst, sel, cls);
        }
    }
    
    return lookUpImpOrForward(inst, sel, cls, behavior | LOOKUP_CACHE);
}
```

到这个函数，我们就清楚的可以看到，其内部会调用`resolveInstanceMethod`函数或者`resolveClassMethod`函数来分别处理对象方法和类方法，此时runtime就会在对应的类中调用如下方法：

```objc
+ (BOOL)resolveInstanceMethod:(SEL)sel; //动态解析实例方法
+ (BOOL)resolveClassMethod:(SEL)sel; //动态解析类方法
```

> 注意：当动态解析完毕，不管用户是否作出了相应处理，runtime都会调用`lookUpImpOrForward`， 重新尝试查找一遍类的消息列表。

#### resolveInstanceMethod

`+ (BOOL)resolveInstanceMethod:(SEL)sel`用来动态解析实例方法，我们需要在运行时动态的将对应的方法实现添加到类实例所对应的类的消息列表中：

```objc
+ (BOOL)resolveInstanceMethod:(SEL)sel {
    if ([NSStringFromSelector(sel) isEqualToString:@"test"]) {
        
        NSLog(@"resolveInstanceMethod: %@", NSStringFromSelector(sel));
        class_addMethod(self, sel, class_getMethodImplementation(self, @selector(unrecoginzedInstanceSelector)), "v@:");
        return YES;
    }
    return [super resolveInstanceMethod:sel];
}

- (void)unrecoginzedInstanceSelector {
    NSLog(@"It's an  unrecoginzed instance selector...");
}
```

#### resolveClassMethod

`+ (BOOL)resolveClassMethod:(SEL)sel`用于动态解析类方法。 我们同样需要将类的实现动态的添加到相应类的消息列表中。

但这里需要注意，调用类方法的“对象”实际也是一个类，而类所对应的类应该是 **`元类`** 。要添加类方法，我们必须把方法的实现添加到 **`元类`** 的方法列表中。

在这里，我们就不能够使用`[self class]`了，它仅能够返回当前的类。而是需要使用`object_getClass(self)`，它其实会返回`isa`所指向的类，即类所对应的元类（注意，因为现在是在类方法里面，`self`所指的是`Class`，而通过`object_getClass(self)`获取`self`的类，自然是元类）。

```objc
+ (BOOL)resolveClassMethod:(SEL)sel {
    if ([NSStringFromSelector(sel) isEqualToString:@"testClass"]) {
        NSLog(@"resolveClassMethod: %@", NSStringFromSelector(sel));
        class_addMethod(self, sel, class_getMethodImplementation(object_getClass(self), @selector(unrecoginzedClassSelector)), "v@:");
        return YES;
    }
    
    return [super resolveClassMethod:sel];
}

+ (void)unrecoginzedClassSelector {
    NSLog(@"It's an  unrecoginzed class selector...");
}
```

> 这里主要弄清楚，类，元类，实例方法和类方法在不同地方存储，就清楚了。
> 关于`class`方法和`object_getClass`方法的区别：
> 当`self`是实例对象时，`[self class]`与`object_getClass(self)`等价，因为前者会调用后者，都会返回对象实例所对应的类。
> 当`self`是类对象时，`[self class]`返回类对象自身，而`object_getClass(self)`返回类所对应的元类。

### 快速转发 Fast Forwarding

当上面的 **动态方法解析** 失败，则进入消息转发流程。所谓消息转发，是将当前消息转发到其它对象进行处理。

从上面的`lookUpImpOrForward`函数的分析中可以看到，当方法查找和动态方法解析都没有找到对应的IMP时则会调用`forward_imp`函数，`forward_imp`只是一个定义：

```objc
 const IMP forward_imp = (IMP)_objc_msgForward_impcache;
```

本质是调用`_objc_msgForward_impcache`函数：

```objc
// runtime源码: objc-msg-arm64.s 中
STATIC_ENTRY __objc_msgForward_impcache

// No stret specialization.
b	__objc_msgForward

END_ENTRY __objc_msgForward_impcache
```

这是一个汇编函数，其内部是调用了`__objc_msgForward`函数：

```objc
// runtime源码: objc-msg-arm64.s 中
ENTRY __objc_msgForward

adrp	x17, __objc_forward_handler@PAGE
ldr	p17, [x17, __objc_forward_handler@PAGEOFF]
TailCallFunctionPointer x17
	
END_ENTRY __objc_msgForward
```

`_objc_msgForward` 也只是个入口，从汇编源码可以很容易看出`_objc_msgForward`实际是调用 `_objc_forward_handler` ：

```objc
// runtime源码: objc-runtime.mm 中
__attribute__((noreturn, cold)) void
objc_defaultForwardHandler(id self, SEL sel)
{
    _objc_fatal("%c[%s %s]: unrecognized selector sent to instance %p "
                "(no message forward handler is installed)", 
                class_isMetaClass(object_getClass(self)) ? '+' : '-', 
                object_getClassName(self), sel_getName(sel), self);
}
void *_objc_forward_handler = (void*)objc_defaultForwardHandler;
```

也就是说，消息转发过程是先将`_objc_msgForward_impcache`强转成`_objc_msgForward`，再调用`_objc_forward_handler`。

此时，如果我们不做任何操作，那么消息转发到这个阶段就会调用`objc_defaultForwardHandler`函数，这个时候我们就能看到最常见的错误 “unrecognized selector sent to instance ....”日志，并且触发了crash。

因为默认的`Handler`干的事儿就是打印日志和触发 crash，那么我们**想要实现消息转发，就需要替换掉默认`Handler`并赋值给`_objc_forward_handler`**，赋值的过程就需要用到`objc_setForwardHandler`函数，它的实现也真是简单粗暴，就是赋值啊：

```objc
// runtime源码: objc-runtime.mm 中
void objc_setForwardHandler(void *fwd, void *fwd_stret)
{
    _objc_forward_handler = fwd;
#if SUPPORT_STRET
    _objc_forward_stret_handler = fwd_stret;
#endif
}
```

So 问题来了，这个`objc_setForwardHandler`函数是何时调用的？它之后的消息转发调用栈又是怎样的？？

要回答上面两个问题，就不是在**Objective-C Runtime**中了，而是在**Core Foundation**中，虽然[CF 是开源的](https://github.com/opensource-apple/CF)，但有意思的是苹果故意在开源的代码中删除了在**CFRuntime.c**文件`__CFInitialize()`函数中调用`objc_setForwardHandler`函数的代码。`__CFInitialize()`函数是在 CF runtime 连接到进程时初始化调用的。从反编译得到的汇编代码中可以很容易跟 C 源码对比出来。

反编译后`__CFInitialize`的汇编代码如下：
![__CFInitialize汇编-c](http://mweb.sandslee.com/2020-03-31-QQ20160614-1@2x.png)

> 汇编语言还是比较好理解的，红色标出的那三个指令就是把 `__CF_forwarding_prep_0` 和 `___forwarding_prep_1___` 作为参数调用 `objc_setForwardHandler` 方法。

然而在苹果提供的`__CFInitialize()`函数源码中对应的代码却被删掉了：
![__CFInitialize函数-c](http://mweb.sandslee.com/2020-03-31-QQ20160614-2@2x.png)

从上面的分析我们可以知道`objc_setForwardHandler`函数是在`__CFInitialize()`函数中调用的，但还是看不出来它之后的调用栈，那么不妨就让程序crash一下看看：

![Snip20200331_2-c](http://mweb.sandslee.com/2020-03-31-Snip20200331_2.png)

这个日志和crash场景熟悉得不能再熟悉了，从调用堆栈可以看出 `_CF_forwarding_prep_0` 函数调用了 `___forwarding___` 函数，接着又调用了 `doesNotRecognizeSelector` 方法，最后抛出异常。

`__CF_forwarding_prep_0` 和 `___forwarding_prep_1___` 函数都调用了 `___forwarding___`，只是传入参数不同。`___forwarding___` 有两个参数，第一个参数为将要被转发消息的栈指针（可以简单理解成 IMP），第二个参数标记是否返回结构体。`__CF_forwarding_prep_0` 第二个参数传入 `0`，`___forwarding_prep_1___` 传入的是 `1`，从函数名都能看得出来。下面是这两个函数的伪代码：

```c
int __CF_forwarding_prep_0(int arg0, int arg1, int arg2, int arg3, int arg4, int arg5) {
    rax = ____forwarding___(rsp, 0x0);
    if (rax != 0x0) { // 转发结果不为空，将内容返回
            rax = *rax;
    }
    else { // 转发结果为空，调用 objc_msgSend(id self, SEL _cmd,...);
            rsi = *(rsp + 0x8);
            rdi = *rsp;
            rax = objc_msgSend(rdi, rsi);
    }
    return rax;
}
int ___forwarding_prep_1___(int arg0, int arg1, int arg2, int arg3, int arg4, int arg5) {
    rax = ____forwarding___(rsp, 0x1);
    if (rax != 0x0) {// 转发结果不为空，将内容返回
            rax = *rax;
    }
    else {// 转发结果为空，调用 objc_msgSend_stret(void * st_addr, id self, SEL _cmd, ...);
            rdx = *(rsp + 0x10);
            rsi = *(rsp + 0x8);
            rdi = *rsp;
            rax = objc_msgSend_stret(rdi, rsi, rdx);
    }
    return rax;
}
```

消息转发的逻辑几乎都写在 `___forwarding___` 函数中了，这个函数的实现比较复杂，反编译出的伪代码也不是很直观。相对完善的伪代码如下：

```c
int __forwarding__(void *frameStackPointer, int isStret) {
  id receiver = *(id *)frameStackPointer;
  SEL sel = *(SEL *)(frameStackPointer + 8);
  const char *selName = sel_getName(sel);
  Class receiverClass = object_getClass(receiver);

  // 1、调用 forwardingTargetForSelector:
  if (class_respondsToSelector(receiverClass, @selector(forwardingTargetForSelector:))) {
    id forwardingTarget = [receiver forwardingTargetForSelector:sel];
    if (forwardingTarget && forwarding != receiver) {
    	if (isStret == 1) {
    		int ret;
    		objc_msgSend_stret(&ret,forwardingTarget, sel, ...);
    		return ret;
    	}
      return objc_msgSend(forwardingTarget, sel, ...);
    }
  }

  // 僵尸对象
  const char *className = class_getName(receiverClass);
  const char *zombiePrefix = "_NSZombie_";
  size_t prefixLen = strlen(zombiePrefix); // 0xa
  if (strncmp(className, zombiePrefix, prefixLen) == 0) {
    CFLog(kCFLogLevelError,
          @"*** -[%s %s]: message sent to deallocated instance %p",
          className + prefixLen,
          selName,
          receiver);
    <breakpoint-interrupt>
  }

  // 2、调用 methodSignatureForSelector 获取方法签名后再调用 forwardInvocation
  if (class_respondsToSelector(receiverClass, @selector(methodSignatureForSelector:))) {
    NSMethodSignature *methodSignature = [receiver methodSignatureForSelector:sel];
    if (methodSignature) {
      BOOL signatureIsStret = [methodSignature _frameDescriptor]->returnArgInfo.flags.isStruct;
      if (signatureIsStret != isStret) {
        CFLog(kCFLogLevelWarning ,
              @"*** NSForwarding: warning: method signature and compiler disagree on struct-return-edness of '%s'.  Signature thinks it does%s return a struct, and compiler thinks it does%s.",
              selName,
              signatureIsStret ? "" : not,
              isStret ? "" : not);
      }
      if (class_respondsToSelector(receiverClass, @selector(forwardInvocation:))) {
        NSInvocation *invocation = [NSInvocation _invocationWithMethodSignature:methodSignature frame:frameStackPointer];

        [receiver forwardInvocation:invocation];

        void *returnValue = NULL;
        [invocation getReturnValue:&value];
        return returnValue;
      } else {
        CFLog(kCFLogLevelWarning ,
              @"*** NSForwarding: warning: object %p of class '%s' does not implement forwardInvocation: -- dropping message",
              receiver,
              className);
        return 0;
      }
    }
  }

  SEL *registeredSel = sel_getUid(selName);

  // 3、检查 selector 是否已经在 Runtime 注册过
  if (sel != registeredSel) {
    CFLog(kCFLogLevelWarning ,
          @"*** NSForwarding: warning: selector (%p) for message '%s' does not match selector known to Objective C runtime (%p)-- abort",
          sel,
          selName,
          registeredSel);
  } // 4、调用 doesNotRecognizeSelector
  else if (class_respondsToSelector(receiverClass,@selector(doesNotRecognizeSelector:))) {
    [receiver doesNotRecognizeSelector:sel];
  } 
  else {
    CFLog(kCFLogLevelWarning ,
          @"*** NSForwarding: warning: object %p of class '%s' does not implement doesNotRecognizeSelector: -- abort",
          receiver,
          className);
  }

  // The point of no return.
  kill(getpid(), 9);
}
```

这么一大坨代码就是整个消息转发路径的逻辑，概括如下：
1. 先调用 `forwardingTargetForSelector` 方法获取新的 target 作为 receiver 重新执行 selector，如果返回的内容不合法（为 `nil` 或者跟旧 receiver 一样），那就进入第二步；
2. 调用 `methodSignatureForSelector` 获取方法签名后，判断返回类型信息是否正确，再调用 `forwardInvocation` 执行 `NSInvocation` 对象，并将结果返回。如果对象没实现 `methodSignatureForSelector` 方法，则进入第三步；
3. 调用 `doesNotRecognizeSelector` 方法，抛出异常；

到这里我们就明白为什么在快速转发阶段我们可以通过实现`forwardingTargetForSelector:`方法来对未实现的方法进行补救了。我们新创建一个BackupTest类，内部实现sendMessage方法，用来当作备用响应者。

```objc
- (id)forwardingTargetForSelector:(SEL)aSelector {
    if ([NSStringFromSelector(aSelector) isEqualToString:@"sendMessage"]) {
        return [[BackUpTest alloc] init]; // BackUpTest对象可以响应sendMessage方法
    }
    
    return [super forwardingTargetForSelector:aSelector];
}
```

其实，在整个消息转发路径中，我们把**快速转发**定义为只是到调用了`forwardingTargetForSelector`方法为止，如果该方法无法处理，则会进入到**常规转发Normal Forwarding**阶段。

### 常规转发 Normal Forwarding

这是消息转发流程中我们可以处理消息的最后一个阶段，这一步本质上和**快速转发**阶段的处理是一样的，都是切换接受消息的对象，但是这一步切换响应目标更复杂一些，快速转发阶段的`forwardingTargetForSelector:`方法里面只需返回一个可以响应的对象就可以了，但是这一步还需要手动将响应方法切换给备用响应对象。

这个阶段分为两个步骤：

#### 1. 获取方法签名

先是通过如下方法返回方法签名：

```objc
- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector;
```

返回的签名是根据方法的参数来封装的。有两种方式创建签名：
1. 手动创建方法签名
    尽量少使用，因为容易创建错误，可以按照[这个规则](https://blog.csdn.net/ssirreplaceable/article/details/53376915)来创建。
    
    ```objc
    NSMethodSignature *sign = [NSMethodSignature signatureWithObjCTypes:"v@:"];
    ```

2. 自动创建方法签名

    使用对象本身的`methodSignatureForSelector:`方法自动获取该SEL对应类别的签名。
    
    ```objc
    BackUpTest *backup = [[BackUpTest alloc] init];
    NSMethodSignature *sign = [backup methodSignatureForSelector:@selector(sendMessage)];
    ```

所以，NSMethodSignature 可以参考如下实现：

```objc
- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector {
    if ([super methodSignatureForSelector:aSelector] == nil) {
        BackUpTest *backup = [[BackUpTest alloc] init];
        return [backup methodSignatureForSelector:@selector(sendMessage)];
    }
    
    return [super methodSignatureForSelector:aSelector];
}
```

#### 2. 转发执行

然后是通过如下方法来将响应方法切换给备用响应对象：

```objc
- (void)forwardInvocation:(NSInvocation *)anInvocation;
```

进入这一步的前提是上面的`methodSignatureForSelector:`方法返回的有签名，这一步的实现可以参考：

```objc
- (void)forwardInvocation:(NSInvocation *)anInvocation {
    SEL sel = anInvocation.selector;
    if ([NSStringFromSelector(sel) isEqualToString:@"sendMessage"]) {
        BackUpTest *backup = [[BackUpTest alloc] init];
        if ([backup respondsToSelector:sel]) {
            [anInvocation invokeWithTarget:backup];
        }
    }
}
```

如果直到这一步依然没法儿处理当前消息，则就会调用 `doesNotRecognizeSelector` 方法抛出异常了。该方法的伪实现如下:

```objc
void -[NSObject doesNotRecognizeSelector:](void * self, void * _cmd, void * arg2) {
    r14 = ___CFFullMethodName([self class], self, arg2);
    _CFLog(0x3, @"%@: unrecognized selector sent to instance %p", r14, self, r8, r9, stack[2048]);
    rbx = _CFMakeCollectable(_CFStringCreateWithFormat(___kCFAllocatorSystemDefault, 0x0, @"%@: unrecognized selector sent to instance %p"));
    if (*(int8_t *)___CFOASafe != 0x0) {
            ___CFRecordAllocationEvent();
    }
    rax = _objc_rootAutorelease(rbx);
    rax = [NSException exceptionWithName:@"NSInvalidArgumentException" reason:rax userInfo:0x0];
    objc_exception_throw(rax);
    return;
}

void +[NSObject doesNotRecognizeSelector:](void * self, void * _cmd, void * arg2) {
    r14 = ___CFFullMethodName([self class], self, arg2);
    _CFLog(0x3, @"%@: unrecognized selector sent to class %p", r14, self, r8, r9, stack[2048]);
    rbx = _CFMakeCollectable(_CFStringCreateWithFormat(___kCFAllocatorSystemDefault, 0x0, @"%@: unrecognized selector sent to class %p"));
    if (*(int8_t *)___CFOASafe != 0x0) {
            ___CFRecordAllocationEvent();
    }
    rax = _objc_rootAutorelease(rbx);
    rax = [NSException exceptionWithName:@"NSInvalidArgumentException" reason:rax userInfo:0x0];
    objc_exception_throw(rax);
    return;
}
```

也就是说我们还可以继续 override `doesNotRecognizeSelector` 或者 **捕获其抛出的异常** 。在这里还是大有文章可做的，但不是本文的重点，就不再继续展开了。

### 小结

在 **动态方法解析 Method Resolver** 、 **快速转发 Fast Forwarding** 以及 **常规转发 Normal Forwarding** 这三步中的每一步，消息接受者都还有机会去处理消息。同时，越往后面处理代价越高，最好的情况是在**第一步就处理消息，这样runtime会在处理完后缓存结果，下回再发送同样消息的时候，可以提高处理效率**。第二步转移消息的接受者也比进入转发流程的代价要小，如果到最后一步`forwardInvocation`的话，就需要处理完整的`NSInvocation`对象了。

最后借用一张消息发送与转发流程图，将整个消息发送过程的核心实现都绘制出来了：

![objc_msgSend_forward-c](http://mweb.sandslee.com/2020-03-31-objc_msgSend&forward.jpg)

