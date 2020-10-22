CppApplication {
	name: "alpmbuild++"

	cpp.cppFlags: ['-Werror=return-type']
	cpp.cxxLanguageVersion: "c++17"

	files: [
		"main.cpp",
	]
}
