class_name GenericCallOnNonGenericFunction

func not_generic(value: int) -> int:
	return value

func test_call() -> void:
	var value: int = not_generic::int(1)
