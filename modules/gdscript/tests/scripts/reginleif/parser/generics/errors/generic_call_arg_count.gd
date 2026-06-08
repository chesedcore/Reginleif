class_name GenericCallArgCount

func one[T](value: T) -> T:
	return value

func test_call() -> void:
	var value: int = one::[int, String](1)
