class_name GenericCallViolatesBound

func node_identity[T: Node](value: T) -> T:
	return value

func test_call() -> void:
	var value: int = node_identity::int(1)
