class_name FunctionGenericInference

func identity[T](value: T) -> T:
	return value

func use_inferred_types() -> void:
	var _inferred_int: int = identity(42)
	var _inferred_string: String = identity("generic")
