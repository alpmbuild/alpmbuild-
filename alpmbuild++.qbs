QtApplication {
	name: "alpmbuild++"

	cpp.cppFlags: ['-Werror=return-type']
	cpp.cxxLanguageVersion: "c++20"

	files: [
		"main.cpp",
	]
}
