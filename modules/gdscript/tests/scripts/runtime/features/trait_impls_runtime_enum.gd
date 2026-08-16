extends Node

class_name TraitRuntimeEnumNode

enum Bool {
	TRUE,
	FALSE,
}

func test():
	print(Bool.TRUE.rt_bool_string())
	var value := Bool.FALSE
	print(value.rt_bool_string())
