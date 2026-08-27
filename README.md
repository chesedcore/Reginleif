# Reginleif Engine
hello hello! this is a little fork made for me and my friends! 'twas made because GDScript was a *little* lacking, and i discovered i had the free will to do things my way! what *is* my way, you ask? well, i've always had a love-hate relationship with GDScript. i truly love the rapid iteration capabilities it offers, but ah well, it lacks a *few* features, *ahem*, to help build the kind of systems heavy games i want to make.

tell ya what, mate. while writing gdscript across half a decade, it felt like my dumbass was being forced to accept a tradeoff that the developers had made. the tradeoff being that the language optimised the developer experience for the first fifty hours of gamedev, and did so by horribly compromising on the next thousand.

i need to feel confident in the code that i write as i am indeed human and make mistakes! sound reasonable? hey, same, we might become good friends then!

## Who is this for?
certainly not for everyone! that's pretty intentional.

this fork is, first and foremost, for me, and my group of friends i hang out with and make games with.

but that's not enough to know if YOU, random stranger (perhaps) would be into this project. so let's talk about that.

this fork is for devs who LIKE gdscript's workflow, engine integration and rapid iteration cycles, but are at their limits (like i) by the limits of the static analysis and the type system. it is aimed at devs building larger, or perhaps longer term projects where compile-time guarantees matter more than maintaining beginner simplicity, who don't want to be told 'har har har har just use C#'

this fork will NOT aim to preserve GDScript as an extremely simplistic dynamic scripting language.

i strongly recommend turning on the static typing error in the project settings. you know which one, riiiight?

## Get started
Compile the engine by `git clone`ing this repository, use `pip install scons` to install `scons`, then compile by typing in just `scons`. Should take a little bit of time. 

If you don't want to go through the hassle of all that, I periodically throw a few prebuilt binaries in the releases section on the right. Ideally, there should be backwards compatibility with projects that use strongly typed GDScript, but back-compat is not the biggest prio for me, even if it *is* a priority.

**It is not guaranteed that the latest release matches nightly.** Compile yourself for the latest version or request another release if it seems too stale.

## Shit I want to add
- type unifier
- structs
- sum types
- errors as values
- exhaustive pattern matching

## Shit I added
- type narrowing
- generics (currently only type-erased)
- nested types
- completely optional braces {} based scoping
- traits (first pass + optimisation passes)
- some minor syntax niceties

## How to use the shit I added

### Type Narrowing
Not a feature you 'use' per se, but static analysis now becomes smarter.
`is Type` type checks now 'narrow' the type in their branch.

```gdscript
var node := get_node(...)
if x is Node2D:
    print(x.position)
```
This marks the third line to be type unsafe in vanilla GDScript, and this can even be seen in the editor, as the line number on the left side of the code editor becomes unlit, showing that the analyser could not prove type safety. It even triggers UNSAFE_PROPERTY_ACCESS and related warnings! 
But this is a problem, because, this operation provably IS safe, so the warning is misleading! it only executes if x is that type, and thus, x's type gets 'narrowed.'

This fork is narrowing aware! It does indeed narrow the above case, along with the following cases:

```gdscript
if x is Node2D and x.get_position().x > 20:
```

```gdscript
if x is not Node2D: return
print(x.position)
```

```gdscript
if x is not Node2D:
    pass
else:
    print(x.position)
```
All the above cases become narrow-aware and thus type safe, along with full code complete! This extends to `is Trait` style checks too (discussed soon in this doc).

### Generics

a feature that lets you specify the type of a variable with a placeholder, usually `T`, where that `T` is usually meant to be replaced later on with a 'concrete type'. 

currently, generics are TYPE-ERASED, which means they reduce to `Variant` in the actual runtime. all of the correctness heavywork is done by cranking static analysis up to the max. soon i intend to reify them, so correctness extends to runtime too.

#### Class-level Generics

declare a class level generic by stating it alongside the class_name identifier in your `class_name` declaration, wrapped in `[brackets]`

```gdscript
class_name Box[T]
```

and then use the generic parameter as a type anywhere in the class body:
```gdscript
class_name Box[T]

var val: T

func _init(arg: T) -> void:
	arg = val
```

multiple parameters are also allowed in the declaration, as such:

```gdscript
class_name Result[T, E]
```

now, you can instance a Box by using the constructor as such:

```gdscript
func _ready() -> void:
	var b := Box.new("waltuh...")
```
Static analysis magically infers that `b` is a `Box[String]`. Assigning `var x: Node = b.val` will give you a compile-time error. try it out!

you can set an "upper bound" on the generics, which are type constraints placed on that generic.
```gdscript
class_name Box[T: Node3D]
```
now you can no longer pass Node2Ds into Box, as T only accepts Node3Ds and its children:
```gdscript
var b := Box.new(AnimatedSprite2D.new())
#compile-time error!
```

to check if a generic is a certain concrete type, you can perform `if x is int:` style comparisons.
```gdscript
func balls(x: T) -> void:
	if x is int: print(x)
```

!!however!! because generics are type-erased as of now, `if x is Box[int]` gets erased to `if x is Box` during the runtime. Please match on the actual value like such: `if x is Box and x.val is int` for now. Yes, this is a problem, and I will address this soon.

#### Function-level generics

to declare function-level generics, you can list them after the function name identifier in `[brackets]`:
```gdscript
func balls[U, V](input: U) -> V:
```

these generics are usually infered via arguments that are passed in automagically.

however, in case you want to explicitly pass in the types for these generic parameters, you may use the
turbobrick `::[]` for this. some of you might be familiar with a similar construct from a certain language.

calling `balls()` with explicit types:
```gdscript
balls::[int, float](32)
```

in case your function only takes in only one function level generic, you may omit the `[brackets]`:
```gdscript
succ::int(32)
```

however, in 90% of cases, you will probably not need the turbobrick, as inference magics away the types for you.

### exporting generics
There are times when you'd rather want to export members set on a generic class. 

Surely you've tried to do this:
```gdscript
class_name Box[T] extends Node
@export var boxed: T
#^^^^^^^^^^^^^^^^^^^ error!
```
The error will state that you cannot prove that T is a serialisable type and thus you cannot export T.

To fulfill this, you **must set an upper bound** on T as something that is serialisable (a built-in, a Resource, a Node, or an enum, as Godot puts it).
For an example:
```gdscript
class_name Box[T: Resource] extends Node
               #  ^^^^^^^^^ since T must be a Resource, T is serialisable
@export var boxed: T
#no errors!
```

Now you can attach this script to a Node and make it active on the editor:
```gdscript
@export var box: Box[Shader] #summons an Export Box where you can fill in the value of boxed restricted to Shaders
```
This is a rather new feature, and I've tried my best to iron out any correctness bugs, but if you see any, please open issues!

### full nested typing

you remember how you could not make an `Array[Dictionary[StringName, Resource]]` in vanilla gdscript?
the reins are unlocked and you can go batshit crazy with your type tetris now:

```gdscript
var x: Array[Dictionary[Node2D, Array[Dictionary[Resource, StringName]]]]
#(please don't actually go that crazy for the sake of future you)
```

a caveat is that arrays/dicts that are typed to generics `[T]` don't have methods that take in `T`. they still want a `Variant` like in vanilla GDScript. of course, all these are planned to be addressed, but that's how it is for now.

### completely optional braces{} based scoping

This is now possible:
```gdscript
func _ready() -> void {
	#your code goes here
}
```

However, funnily enough, **this is not supported**:
```gdscript
func _ready() -> void
{
	#your code
}
```
this is because the `NEWLINE` token forces the parser into an ambiguous position that is very hard to solve without somehow breaking abstract functions. so just use KnR braces if you intend to use this feature lol

oh, yeah! you can skip the `pass` keyword when using braces for empty blocks. this is completely legal:
```gdscript
func _ready() -> void {}
```

A very unfortunate consequence of optional braces and then whitespace scoping being able to be mixed is that monstrous abominations straight out of Dante's Inferno are possible:
```gdscript
func _ready() -> void {
	var x := 3
	if x > 3 {
		print("6")
	} else:
		print("7")
}
```

have fuuun with that!

### Traits
Traits are a feature that let you specify compile-time contracts of behaviour to either your own types or (as a form of ad-hoc polymorphism) apply them to Godot's existent types. This feature is extremely similar to what is known as 'interfaces' in other languages, and is heavily inspired by a certain other programming language's implementation of traits.

#### how to declare a trait
you can only declare a trait in a new file. create a new gdscript file (a new extension is not required), and write `trait` followed by the name of the trait:
```gdscript
trait Rollable
```
you can then specify the "contract" associated with this trait, i.e, the required signature that a type implementing this trait must have:

```gdscript
trait Rollable

func roll() -> void
func is_rolling() -> bool
```

now any type that wants to implement this trait must necessarily implement a `roll()` method and a `is_rolling()` method, as was specified by the trait contract.


#### how to implement a trait
you can 'implement'(`impl`) a trait `for` a target type with this syntax: `impl TraitName for TargetType:`
```gdscript
class_name Balls

impl Rollable for Balls:
	func roll() -> void: print("ow")
	func is_rolling() -> bool: return false
```

this allows you to call `x.roll()` on an `x` that is of the type `Balls`. 

you can only implement a trait in:
- the file that the trait is declared in, or
- the target class' file that the trait targets

you can target *any* type in the engine for a trait. 
this quite literally means you can implement a trait for very commonly used built-in types, like `int`.

as an example:
```gdscript
trait ExtInt
func min(other: int) -> int

impl ExtInt for int:
	func min(other: int) -> int:
		return mini(self, other)
```
this code grants any `int` the ability to call the method `min`:
```gdscript
func _ready() -> void:
	print(3.min(2)) #prints 2
```
of course, you can use this to confer methods to anything in the engine, such as `String` or `Node`.

#### trait bounds
you can narrow the parameters that a function can accept by "bounding" its inputs or outputs by a trait using the `impl Trait` bound syntax:
```gdscript
func only_rollables[T: impl Rollable](x: T) -> T:
```
now this function accepts and returns any T that implements `Rollable`. multiple bounds can be described as such: `impl Rollable + ExtInt`.

to have a variable that accepts any type that implements a trait:
```gdscript
var t: impl Rollable
```
this bounds `t` to only accepting values that `impl Rollable`.

#### default methods
you can implement default methods for traits to give any class implementing that trait the option to implement that method or not. if they don't, the default is used instead. the use cases for this are that you can use this to hack some duck-typed behaviour using `self`, or give the target access to a composed function that combines the functions that they implement.

you can specify a default implementation by simply writing it as the  method signature's body:
```gdscript
trait Rollable
func roll() -> void:
	print(self, " is rolling")
func is_rolling() -> bool
```
now a target need only implement `is_rolling()`, and `roll()` will be implemented by default for them.

#### type tests
you can test for traits at runtime using `is TraitName` as you would with `is` checks for classes.
```gdscript
func _on_body_entered(body: PhysicsBody2D) -> void:
	if body is Rollable: body.roll()
```

#### impl shorthands
for convenience, if you are implementing a trait in its trait file, you can omit the name of the trait in the `impl` block.
```gdscript
trait Rollable
impl for Balls:
	pass
```
this is the same as `impl Rollable for Balls`.

similarly, you can omit the name of the type target that the trait is being implemented for (along with the `for` keyword) when you are implementing a trait for a class in its own class file.
```gdscript
class_name Balls
impl Rollable:
	pass
```
this is the same as `impl Rollable for Balls`, of course.

#### static vs dynamic dispatch
using a generic to bound a trait counts as static dispatch:
```gdscript
func thing[T: impl Rollable](y: T) -> void:
	var x: T
```
this forces y and x to become the same type concrete in actual code. this is recommended over dynamic dispatch for performance reasons.

however, using the `impl Rollable` itself as a type parameter results in it being counted as dynamic dispatch.
```gdscript
func thing(y: impl Rollable) -> void:
	var x: impl Rollable
```
these are both dynamically dispatched types, so x and y are allowed to be different concrete types. it is more flexible but owing to dyn dispatch is less performant.

i invite you to read the source in modules/gdscript/gdscript_trait_analyzer for a more thorough rundown on how this works. behind the hood, dynamic trait dispatch creates a hidden generic to facilitate passing generic args. of course, you can always ask me for implementation details too.

### syntactical sugar

#### fluent multiline chaining
in vanilla gdscript, you needed `\` if you were chaining methods across multiple lines:
```gdscript
stage                          \
  .query()                     \
  .both_sides()                \
  .and_then(draw_2)
```

i have removed the need for this ugly `\`. you can now chain methods freely across multiple lines:
```gdscript
stage                          
  .query()                     
  .both_sides()                
  .and_then(draw_2)
```
and things shall simply work.

### Some more caveats
- Godot's `core` is rotten. Generics can LIE to you at runtime because static analysis is turned off for Variant-typed variables!!! (I didn't add this, this is Godot's default behaviour) Use static typing everywhere lest you want to run into undefined behaviour with generics.
- Dynamic trait dispatch as a return position is not supported. `-> impl Trait` specifically. you should bound this to a generic and return that generic type instead. this will be fixed in future passes.
- I'm not fucking omniscient bro. There might be bugs, and I ask you to REPORT THEM!! catch my ass on discord at monarch_zero or open an issue here.

## Backwards compat breaks
There are none known so far that have not been fixed. Yay!


## Motivation

Why not contribute to Godot upstream itself? Well, I WANT to. But I have a few issues with that...

0. GDScript dev team and I will probably never see eye-to-eye. We are solar systems apart with our philosophy towards languages. In what way, you ask? Ah, let me ramble a bit. I generally believe that beginner-friendliness should not come at the cost of correctness or large-scale scalability. The language should *let you grow.* I also think that this difference in interests is fine, that's what the magic of open source is. 
1. I want traits, sum types, exhaustive matching, structs (and not in the "just make object leaner" way), PROPER ERROR HANDLING, tuples, stronger static guarantees, hell I'd even be delighted if GDScript dropped dynamic typing altogether, as I largely consider that to be a beginner trap. I am very glad that the Godot project is at least open-source, so that I may leech off of upstream and add my own changes. This project exists as a 'here you go!' for people who want the same rapid-iteration power GDScript is known for while allowing expression of stronger invariants, without bending knees to C#.
2. Godot PR review is GLACIAL. By glacial I do mean INSANELY GLACIAL. New features take YEARS to be accepted. The dev team actually just hates it when you touch `core`. I won't pretend they're evil and do it just because they're lazy or something (cough cough Redot), they have very real reasons to take as much time as they take, Godot being the backbone of millions of indie gamedevs around. but obviously I'm unhappy with the pace, so I'll go ahead implementing some of these myself.
3. I want the freedom to make mistakes with my PRs. I don't like C++ as a language and how much it relies to on me being completely fucking omniscient. Speaking of which...
4. If I had a Rust dependency (something the Godot team will never accept) later on like Cranelift to enable JITs, I want a platform to be able to do that.
5. I also just want to have fun adding silly little things! Professionalism is the antithesis of fun. I want the freedom to add a little life to the engine.
6. Lastly, I have an amazing group of gamedev friends who have a vested interest in this specific project. 

## Lmao why not c#
because i don't wanna. if you want to, good! go ahead.

## you should've waited for GDType/Big Core Rewrite/Godot 5/Weekly Steel Ball Run
i don't wanna. i prioritise usability now. i'm not saying GDType and shit are bad, i'm saying i'm an impatient kid.

## special thanks
- the Free Will dev team, for stress-testing this fork
- polanas, for lending me compute for compilation, and helping benchmark bottlenecks
- Hannah (jmejuniper) for the awesome custom-built icon for this fork!
- regulars in InboundShovel discord's godot channel for being awesome
- godot upstream for getting me into gamedev in the first place
- you!
