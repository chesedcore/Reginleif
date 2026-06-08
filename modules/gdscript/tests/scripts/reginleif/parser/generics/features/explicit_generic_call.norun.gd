class_name ExplicitGenericCall

func identity[T](value: T) -> T:
	return value

func use_explicit_type_argument() -> void:
	var _explicit_int: int = identity::int(42)
