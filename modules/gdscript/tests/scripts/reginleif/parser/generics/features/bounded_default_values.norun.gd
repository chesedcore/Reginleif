class_name BoundedDefaultValues[T: int]

var member_value: T = 3

func with_default(parameter_value: T = 4) -> T:
	return parameter_value
