#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <vector>

#include "util.h"

class File
{
	u64 _length;
	string _name;
	std::fstream _stream;

public:
	File(const string &path)
	{
		_length = fs::file_size(path);
		_stream.open(path, std::ios::in | std::ios::binary);
		_name = fs::path(path).filename();
	}
	~File()
	{
		_stream.close();
	}
	fn length()->u64 const
	{
		return _length;
	}
	fn name()->string &
	{
		return _name;
	}
	fn read_span(u64 from, u64 to)->string
	{
		var buf = new char[to - from];
		_stream.seekg(from);
		_stream.read(buf, to - from);

		return string(buf);
	}
	fn pos_to_line_col(u64 pos)->std::tuple<u64, u64>
	{
		u64 line = 1;
		u64 col = 1;

		u64 stream_pos = 0;
		_stream.seekg(0);
		char cur;

		while (stream_pos < pos)
		{
			col++;
			stream_pos++;

			_stream.read(&cur, 1);

			if (cur == '\n')
			{
				col = 1;
				line++;
			}
		}

		return {line, col};
	}
	// line and col are human-based (starts at 1)
	fn line_col_to_pos(u64 line, u64 col)->u64
	{
		u64 cur_line = 1;
		u64 cur_col = 1;

		u64 stream_pos = 0;
		_stream.seekg(0);
		char cur;

		while (true)
		{
			if (line == cur_line && col == cur_col)
			{
				return stream_pos;
			}

			_stream.read(&cur, 1);
			stream_pos++;

			cur_col++;
			if (cur == '\n')
			{
				cur_col = 1;
				cur_line++;
				continue;
			}
		}

		throw -1;
	}
	fn line_start_and_end_for_pos(u64 pos)->std::tuple<u64, u64>
	{
		u64 start = 1;
		u64 end = 1;

		u64 stream_pos = 0;
		_stream.seekg(0);
		char cur;

		while (stream_pos < pos)
		{
			_stream.read(&cur, 1);
			stream_pos++;

			if (cur == '\n')
			{
				start = stream_pos + 1;
			}
		}

		while (cur != '\n')
		{
			_stream.read(&cur, 1);
			stream_pos++;
		}

		end = stream_pos - 1;

		return {start, end};
	}
};

struct Diagnostic
{
	string message;
	std::tuple<u64, u64> highlight_span;
};

class FileSet
{
	std::vector<File *> _files;
	std::list<u64> _file_lengths;

public:
	FileSet()
	{
	}
	~FileSet()
	{
		for (auto file : _files)
		{
			delete file;
		}
	}

public:
	fn add_file(File *file)
	{
		_files.push_back(file);
		_file_lengths.push_back(file->length());
	}
	fn file_for_pos(u64 pos)->File *
	{
		size_t idx = 0;
		u64 total_sizes = 0;

		for (auto item : _file_lengths)
		{
			total_sizes += item;

			if (pos <= total_sizes)
			{
				return _files[idx];
			}

			idx++;
		}

		throw -1;
	}
	fn map_to_local_file(u64 pos)->u64
	{
		u64 total_sizes = 0;

		for (auto item : _file_lengths)
		{
			total_sizes += item;

			if (pos <= total_sizes)
			{
				return pos - (total_sizes - item);
			}
		}

		throw -1;
	}
	fn map_from_local_file(File *file, u64 pos)->u64
	{
		u64 total_sizes = 0;

		for (auto item : _files)
		{
			total_sizes += item->length();

			if (item == file)
			{
				return (total_sizes - item->length()) + pos;
			}
		}

		throw -1;
	}
	fn render_diagnostic(const Diagnostic &diagnostic)
	{
		let[start, end] = diagnostic.highlight_span;
		let filename = file_for_pos(start)->name();

		let[startLine, startCol] = file_for_pos(start)->pos_to_line_col(map_to_local_file(start));
		let[endLine, endCol] = file_for_pos(end)->pos_to_line_col(map_to_local_file(end));
		let[a, b] = file_for_pos(start)->line_start_and_end_for_pos(map_to_local_file(start));
		let str = file_for_pos(start)->read_span(a, b);

		std::cout << "some error at " << filename << ":" << startLine << ":" << startCol << " - " << endLine << ":" << endCol << ": " << diagnostic.message << std::endl;
		std::cout << startLine << " | " << str << std::endl;
		std::cout << std::string(std::to_string(startLine).length(), ' ') << " " << std::string(startCol, ' ') << std::string(endCol - startCol, '^') << std::endl;
	}
};

int main(int argc, char *argv[])
{
	var fileset = FileSet();

	for (var i = 1; i < argc; i++)
	{
		var file = new File(argv[i]);

		fileset.add_file(file);
	}

	let fi = fileset.file_for_pos(1000);
	let start = fileset.map_from_local_file(fi, fi->line_col_to_pos(11, 6));
	let end = fileset.map_from_local_file(fi, fi->line_col_to_pos(11, 13));

	let diagnostic = Diagnostic{
		.message = "gender do a hecf",
		.highlight_span = {start, end}};
	fileset.render_diagnostic(diagnostic);

	return 0;
}
