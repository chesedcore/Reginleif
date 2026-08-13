extends Node

func test():
	name = "TraitRunner"

	print(3.rt_min(2))
	print(3.rt_min(7))
	print(4.rt_times(5))
	print(10.rt_add_all())
	print(10.rt_add_all(1))
	print(10.rt_add_all(1, 2, 3))

	var int_callable: Callable = 7.rt_callable_value
	print(int_callable.call())

	print("gd".rt_wrap())
	print("gd".rt_wrap("<", ">"))
	print("gd".rt_shout())

	print(rt_named())
	print(rt_named("self"))
