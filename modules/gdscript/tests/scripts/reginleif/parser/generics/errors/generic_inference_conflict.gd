class_name GenericInferenceConflict

func choose[T](first: T, second: T) -> T:
	return first

func test_call() -> void:
	var value: int = choose(1, "two")
