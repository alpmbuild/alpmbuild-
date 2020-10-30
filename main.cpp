#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <vector>
#include <variant>
#include <optional>
#include <functional>

#include "util.h"

template <typename... Args>
std::string string_format(const std::string &format, Args... args)
{
	size_t size = snprintf(nullptr, 0, format.c_str(), args...) + 1;
	if (size <= 0)
	{
		throw std::runtime_error("Error during formatting.");
	}
	std::unique_ptr<char[]> buf(new char[size]);
	snprintf(buf.get(), size, format.c_str(), args...);
	return std::string(buf.get(), buf.get() + size - 1);
}

struct Diagnostic
{
	string message;
	std::tuple<u64, u64> highlight_span;
};

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
		_stream.exceptions(std::ios::failbit);
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
		if (from == to) {
			var buf = rune();
			_stream.seekg(from);
			_stream.read(&buf, 1);

			return string(&buf);
		}

		var buf = new rune[to - from];
		_stream.seekg(from);
		_stream.read(buf, to - from);

		return string(buf);
	}
	fn read_pos(u64 pos)->rune
	{
		rune ret;

		_stream.seekg(pos);
		if (!_stream.get(ret)) {
			if (_stream.eof()) {
				return EOF;
			} else {
				throw std::runtime_error("error reading file");
			}
		}

		return ret;
	}
	fn pos_to_line_col(u64 pos)->std::tuple<u64, u64>
	{
		u64 line = 1;
		u64 col = 1;

		u64 stream_pos = 0;
		_stream.seekg(0);
		rune cur;

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
		rune cur;

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
		rune cur;

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
	fn render_diagnostic(const Diagnostic &diagnostic)
	{
		let[start, end] = diagnostic.highlight_span;
		let filename = name();

		let[startLine, startCol] = pos_to_line_col(start);
		let[endLine, endCol] = pos_to_line_col(end);
		let[a, b] = line_start_and_end_for_pos(start);
		let str = read_span(a, b);

		std::cout << "some error at " << filename << ":" << startLine << ":" << startCol << " - " << endLine << ":" << endCol << ": " << diagnostic.message << std::endl;
		std::cout << startLine << " | " << str << std::endl;
		std::cout << std::string(std::to_string(startLine).length(), ' ') << " " << std::string(startCol, ' ') << std::string(endCol - startCol, '^') << std::endl;
	}
};

class Context
{
	File *_file = nullptr;
	u64 _currentPos = 0;

public:
	Context(File *fs) : _file(fs)
	{
	}
	fn current()->rune
	{
		return _file->read_pos(_currentPos);
	}
	fn next()->rune
	{
		_currentPos++;
		return current();
	}
	fn peek_next()->rune
	{
		return _file->read_pos(_currentPos + 1);
	}
	fn previous()->rune
	{
		_currentPos--;
		return current();
	}
	fn peek_previous()->rune
	{
		return _file->read_pos(_currentPos - 1);
	}
	fn pos()->u64
	{
		return _currentPos;
	}
	fn goto_pos(u64 pos)
	{
		_currentPos = pos;
	}
	fn seek(u64 offset)
	{
		_currentPos += offset;
	}
	fn read(u64 amount)->string
	{
		let result = _file->read_span(_currentPos, _currentPos + amount);
		return result;
	};
};

template <typename T, typename F>
using Result = std::variant<T, F>;

// CI: CaseInsensitive
enum class StringTags
{
	Expected,
	ExpectedString,
	ExpectedStringCI,
	Unexpected,
	UnexpectedString,
	UnexpectedStringCI,
	Message,
};

template <StringTags K>
struct TaggedString
{
	TaggedString(string str = string()) : val(str) {}
	string val;
	string &operator*()
	{
		return val;
	}
	string *operator->()
	{
		return &val;
	}
};

using Expected = TaggedString<StringTags::Expected>;
using ExpectedString = TaggedString<StringTags::ExpectedString>;
using ExpectedStringCI = TaggedString<StringTags::ExpectedStringCI>;
using Unexpected = TaggedString<StringTags::Unexpected>;
using UnexpectedString = TaggedString<StringTags::UnexpectedString>;
using UnexpectedStringCI = TaggedString<StringTags::UnexpectedStringCI>;
using Message = TaggedString<StringTags::Message>;
using Exception = std::exception;

struct Error;

using ErrorMessage =
	std::variant<
		Expected,
		ExpectedString,
		ExpectedStringCI,
		Unexpected,
		UnexpectedString,
		UnexpectedStringCI,
		Message,
		Exception,
		Error *>;

struct Error
{
	u64 position;
	std::list<ErrorMessage> messages;
};

template <typename V>
using ParserResult = Result<V, Error>;

template <typename V>
fn holds_failure(const ParserResult<V> &res)->bool
{
	return std::holds_alternative<Error>(res);
}

template <class V>
class Parser;
template <class V>
Parser<V> *parser_from(std::function<ParserResult<V>(Context &)>);

template <typename T>
class Parser
{
public:
	virtual ~Parser() = 0;
	virtual ParserResult<T> parse(Context &ctx) = 0;

	template <class F>
	Parser<F> *map(std::function<F(T)> mapper)
	{
		return parser_from<F>([this, mapper](Context &ctx) -> ParserResult<F> {
			let result = parse(ctx);
			if (holds_failure(result))
			{
				return result;
			}
			ParserResult<F> res;
			res = mapper(std::get<T>(result));
			return res;
		});
	}
	template <class F>
	Parser<F> *then(Parser<F> *parser)
	{
		return parser_from<F>([this, parser](Context &ctx) -> ParserResult<F> {
			let result = parse(ctx);
			if (holds_failure(result))
			{
				ParserResult<F> res;
				res = std::get<Error>(result);
				return res;
			}
			return parser->parse(ctx);
		});
	}
	Parser<T> *or_parse(Parser<T> *parser)
	{
		return parser_from<T>([this, parser](Context &ctx) -> ParserResult<T> {
			let result = parse(ctx);
			if (!holds_failure(result))
			{
				return result;
			}
			return parser->parse(ctx);
		});
	}
	template <class F>
	Parser<std::variant<T, F>> *or_parse(Parser<F> *parser)
	{
		using ReturnType = std::variant<T, F>;

		return parser_from<ReturnType>(
			[this, parser](Context &ctx) -> ParserResult<ReturnType> {
				ParserResult<ReturnType> res;
				ReturnType ret;

				let result = parse(ctx);
				if (!holds_failure(result))
				{
					ret = std::get<T>(result);
					res = ret;
					return res;
				}

				let otherResult = parser->parse(ctx);
				if (!holds_failure(otherResult))
				{
					ret = std::get<F>(otherResult);
					res = ret;
					return res;
				}

				Error err = std::get<Error>(otherResult);
				err.messages.push_back(std::get<Error>(result));
				ret = err;
				res = ret;

				return res;
			});
	}

	// template <class F> Parser<F> *ThenReturn(F value) {
	// 	return ParserFrom<F>([this, value](Context &ctx) -> Result<F> {
	// 	const auto ret = this->operator()(ctx);
	// 	if (std::holds_alternative<Failure>(ret)) {
	// 		const auto fail = std::get<Failure>(ret);
	// 		return NewFailure<F>(fail);
	// 	}
	// 	return NewSuccess(value);
	// 	});
	// }
	// Parser<T> *OrReturn(T value) {
	// 	return ParserFrom<T>([this, value](Context &ctx) -> Result<T> {
	// 	const auto ret = this->operator()(ctx);
	// 	if (std::holds_alternative<Failure>(ret)) {
	// 		return NewSuccess(value);
	// 	}
	// 	return ret;
	// 	});
	// }
	// template <class F> Parser<std::tuple<T, F>> *ThenAlso(Parser<F> *parser) {
	// 	using ReturnType = std::tuple<T, F>;

	// 	return ParserFrom<ReturnType>(
	// 		[this, parser](Context &ctx) -> Result<ReturnType> {
	// 		Holder hold(ctx);

	// 		Result<ReturnType> res;
	// 		ReturnType ret;

	// 		const auto firstResult = this->operator()(ctx);
	// 		if (std::holds_alternative<Failure>(firstResult)) {
	// 			return NewFailure<ReturnType>(std::get<Failure>(firstResult));
	// 		}

	// 		const auto secondResult = parser->operator()(ctx);
	// 		if (std::holds_alternative<Failure>(secondResult)) {
	// 			return NewFailure<ReturnType>(std::get<Failure>(secondResult));
	// 		}

	// 		std::get<0>(ret) = std::get<T>(firstResult);
	// 		std::get<1>(ret) = std::get<F>(secondResult);

	// 		res = ret;
	// 		return hold.Wrap(res);
	// 		});
	// }
	// template <class F> Parser<T> *Before(Parser<F> *parser) {
	// 	return ::Map<T>([](T kind, F) -> T { return kind; }, this, parser);
	// }
	template <class F>
	Parser<T> *between(Parser<F> *parser)
	{
		return parser_from<T>([this, parser](Context &ctx) -> ParserResult<T> {
			let result = parser->parse(ctx);
			if (holds_failure(result))
			{
				return ParserResult<T>(std::get<Error>(result));
			}

			let result2 = parse(ctx);
			if (holds_failure(result2))
			{
				return result2;
			}

			let result3 = parser->parse(ctx);
			if (holds_failure(result3))
			{
				return ParserResult<T>(std::get<Error>(result));
			}

			return result2;
		});
	}
	Parser<std::list<T>> *many()
	{
		return parser_from<std::list<T>>([this](Context &ctx) -> ParserResult<std::list<T>> {
			std::list<T> ret;

			while (true)
			{
				auto result = parse(ctx);
				if (holds_failure(result))
				{
					return ParserResult<std::list<T>>(ret);
				}
				else
				{
					ret.push_back(std::get<T>(result));
				}
			}
		});
	}
	// Parser<QList<T>> *Repeated(uint n) {
	// 	Q_ASSERT(n > 0);
	// 	return ParserFrom<QList<T>>([this, n](Context &ctx) -> Result<QList<T>> {
	// 	Holder hold(ctx);

	// 	QList<T> ret;
	// 	ret.reserve(n);

	// 	for (auto i = 0; i < n; ++i) {
	// 		auto result = this->operator()(ctx);
	// 		if (std::holds_alternative<Failure>(result)) {
	// 		const auto fail = std::get<Failure>(result);
	// 		return NewFailure<QList<T>>(fail);
	// 		}
	// 		ret << std::get<T>(result);
	// 	}

	// 	return hold.Wrap(NewSuccess(ret));
	// 	});
	// }
	// template <class F> Parser<QList<T>> *Until(Parser<F> *terminator) {
	// 	return ParserFrom<QList<T>>(
	// 		[this, terminator](Context &ctx) -> Result<QList<T>> {
	// 		Holder hold(ctx);

	// 		QList<T> ret;

	// 		while (true) {
	// 			auto result = this->operator()(ctx);
	// 			if (std::holds_alternative<Failure>(result)) {
	// 			const auto fail = std::get<Failure>(result);
	// 			return NewFailure<QList<T>>(fail);
	// 			} else {
	// 			ret << std::get<T>(result);
	// 			}
	// 			auto terminatorResult = terminator->operator()(ctx);
	// 			if (std::holds_alternative<Failure>(terminatorResult)) {
	// 			continue;
	// 			} else {
	// 			return hold.Wrap(NewSuccess(ret));
	// 			}
	// 		}
	// 		});
	// }
	// Parser<QString> *ManyString() {
	// 	return this->Many()->template Map<QString>(
	// 		[](QStringList strlist) { return strlist.join(""); });
	// }
};

template <class T>
Parser<T>::~Parser<T>() {}

template <typename V>
fn attempt(Parser<V> *p)->Parser<V> *
{
	return parser_from<V>([p](Context &ctx) -> ParserResult<V> {
		u64 incoming_pos = ctx.pos();

		let result = p->parse(ctx);
		if (holds_failure(result))
		{
			ctx.goto_pos(incoming_pos);
		}
		return result;
	});
}

template <class Ret, typename Mapper, class... Ts>
Parser<Ret> *map(Mapper mapper, Parser<Ts> *... parsers)
{
	return map<Ret, Mapper, Ts...>(mapper, parsers..., std::make_index_sequence<sizeof...(Ts)>());
}

template <class Ret, typename Mapper, class... Ts, size_t... Is>
Parser<Ret> *map(Mapper mapper, Parser<Ts> *... parsers, std::index_sequence<Is...>)
{
	return parser_from<Ret>([=](Context &ctx) -> ParserResult<Ret> {
		std::tuple<Ts...> rets;
		std::optional<Error> fail;

		(std::visit(
			 [&](auto &&arg) {
				 using T = std::decay_t<decltype(arg)>;
				 if constexpr (std::is_same<T, Error>::value)
				 {
					 fail = arg;
				 }
				 else
				 {
					 std::get<Is>(rets) = arg;
				 }
			 },
			 parsers->parse(ctx)),
		 ...);

		if (fail.has_value())
		{
			return ParserResult<Ret>(fail.value());
		}
		return ParserResult<Ret>(std::apply(mapper, rets));
	});
}

template <class T>
Parser<T> *parser_from(std::function<ParserResult<T>(Context &)> parser)
{
	class ParserSub : public Parser<T>
	{
	public:
		std::function<ParserResult<T>(Context &)> func;
		~ParserSub() override {}
		ParserResult<T> parse(Context &ctx) override {
			return func(ctx);
		}
	};
	auto ret = new ParserSub;
	ret->func = parser;
	return ret;
}

auto str(const string &str) -> Parser<string> *
{
	return parser_from<string>([str](Context &ctx) -> ParserResult<string> {
		ParserResult<string> res;

		let incoming_pos = ctx.pos();
		let read = ctx.read(str.length());

		if (read.length() < str.length() || read != str)
		{
			ErrorMessage unexpectedMsg;
			unexpectedMsg.emplace<UnexpectedString>(read);
			ErrorMessage expected;
			expected.emplace<ExpectedString>(str);

			res = Error{
				.position = incoming_pos,
				.messages = std::list<ErrorMessage>{
					unexpectedMsg,
					expected}};
			return res;
		}

		res = read;
		return res;
	});
}

let spaces =
	parser_from<std::monostate>([](Context &ctx) -> ParserResult<std::monostate> {
		while (true)
		{
			let ch = ctx.current();

			if (!isspace(ch))
			{
				return ParserResult<std::monostate>{};
			}

			ctx.next();
		}
	});

let to_newline =
	parser_from<string>([](Context &ctx) -> ParserResult<string> {
		string ret;

		while (true)
		{
			let ch = ctx.current();

			if (ch != '\n' && ch != EOF)
			{
				ret += ch;
			}
			else
			{
				return ParserResult<string>(ret);
			}

			ctx.next();
		}

		return ParserResult<string>(ret);
	});

let ident =
	parser_from<string>([](Context &ctx) -> ParserResult<string> {
		string ret;

		while (true)
		{
			let ch = ctx.current();

			if (isalnum(ch) || ch == '(' || ch == ')' || ch == '_')
			{
				ret += ch;
			}
			else
			{
				return ParserResult<string>(ret);
			}

			ctx.next();
		}
	});

struct Statement
{
	string lhs;
	string rhs;
};

int main(int argc, rune *argv[])
{
	let newline = str("\n");
	let colon = str(":");
	let statement = map<Statement>(
		[](string &lhs, string &, string &rhs) {
			return Statement{
				.lhs = lhs,
				.rhs = rhs};
		},
		ident, colon->between(spaces), to_newline);

	let anything = statement;

	let file = anything->between(spaces)->many();

	File f(argv[1]);

	Context ctx(&f);

	let result = file->parse(ctx);
	if (holds_failure(result))
	{
		let failure = std::get<Error>(result);
		let [line, col] = f.pos_to_line_col(failure.position);

		std::cout << "failed! " << f.name() << ":" << line << ":" << col << std::endl;

		for (auto msg : failure.messages)
		{
			if (std::holds_alternative<Expected>(msg))
			{
				std::cout << "Expected: '" << std::get<Expected>(msg).operator*() << "'" << std::endl;
			}
			if (std::holds_alternative<ExpectedString>(msg))
			{
				std::cout << "ExpectedString: '" << std::get<ExpectedString>(msg).operator*() << "'" << std::endl;
			}
			if (std::holds_alternative<ExpectedStringCI>(msg))
			{
				std::cout << "ExpectedStringCI: '" << std::get<ExpectedStringCI>(msg).operator*() << "'" << std::endl;
			}
			if (std::holds_alternative<Unexpected>(msg))
			{
				std::cout << "Unexpected: '" << std::get<Unexpected>(msg).operator*() << "'" << std::endl;
			}
			if (std::holds_alternative<UnexpectedString>(msg))
			{
				std::cout << "UnexpectedString: '" << std::get<UnexpectedString>(msg).operator*() << "'" << std::endl;
			}
			if (std::holds_alternative<UnexpectedStringCI>(msg))
			{
				std::cout << "UnexpectedStringCI: '" << std::get<UnexpectedStringCI>(msg).operator*() << "'" << std::endl;
			}
			if (std::holds_alternative<Message>(msg))
			{
				std::cout << "Message: '" << std::get<Message>(msg).operator*() << "'" << std::endl;
			}
		}

		return 0;
	}

	return 0;
}
