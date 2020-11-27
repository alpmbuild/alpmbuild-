#pragma once

#include <QBuffer>
#include <QDebug>
#include <variant>

struct Context
{
	QBuffer Buf;
	qint64 FailedAt;
};

struct Failure
{
	QString expected;
	QString got;
	qint64 position;
	QList<Failure *> orFail;
};

template <typename T, typename... Ts>
T Must(std::variant<Ts...> variant)
{
	Q_ASSERT(std::holds_alternative<T>(variant));
	return std::get<T>(variant);
}

template <class T>
using Result = std::variant<T, Failure>;

struct Holder
{
private:
	Context &ctx;
	qint64 pos;

public:
	bool Succeeded = false;
	Holder(Context &ctx) : ctx(ctx) { pos = ctx.Buf.pos(); }
	~Holder()
	{
		if (!Succeeded)
		{
			ctx.FailedAt = pos;
			ctx.Buf.seek(pos);
		}
	}
	template <class T>
	Result<T> Wrap(const Result<T> &result)
	{
		if (std::holds_alternative<T>(result))
		{
			Succeeded = true;
		}
		return result;
	}
};

template <class... Ts>
struct Overloaded : Ts...
{
	using Ts::operator()...;
};

template <class T>
QDebug operator<<(QDebug debug, const Result<T> &data)
{
	QDebugStateSaver saver(debug);
	if (std::holds_alternative<T>(data))
	{
		debug.nospace() << "Success<" << QMetaType::fromType<T>().name().data()
						<< ">(" << std::get<T>(data) << ")";
	}
	else
	{
		debug.nospace() << "Failure(expected=" << std::get<Failure>(data).expected
						<< ", got=" << std::get<Failure>(data).got << ")";
	}
	return debug;
}

template <class T>
Result<T> NewFailure(const Failure &fail)
{
	Result<T> ret = fail;
	return ret;
}

template <class T>
Result<T> NewSuccess(const T &val)
{
	Result<T> ret = val;
	return ret;
}

template <class T>
class Parser;

template <class T>
Parser<T> *ParserFrom(std::function<Result<T>(Context &)>);

template <class Ret, typename Mapper, class... Ts>
Parser<Ret> *Map(Mapper mapper, Parser<Ts> *... parsers);

template <class T>
class Parser
{
public:
	virtual ~Parser() = 0;
	virtual Result<T> operator()(Context &) = 0;
	Context *ctx;
	Result<T> Parse(const QString &str)
	{
		auto ba = str.toUtf8();
		ctx = new Context{QBuffer(&ba)};
		ctx->Buf.open(QIODevice::ReadOnly);
		return this->operator()(*ctx);
	}
	template <class F>
	Parser<F> *Map(std::function<F(T)> mapper)
	{
		return ParserFrom<F>([this, mapper](Context &ctx) -> Result<F> {
			Holder hold(ctx);
			const auto result = this->operator()(ctx);
			if (std::holds_alternative<Failure>(result))
			{
				return NewFailure<F>(std::get<Failure>(result));
			}
			return hold.Wrap(NewSuccess(mapper(std::get<T>(result))));
		});
	}

	template <class F>
	Parser<F> *Then(Parser<F> *parser)
	{
		return ParserFrom<F>([this, parser](Context &ctx) -> Result<F> {
			Holder hold(ctx);
			const auto result = this->operator()(ctx);
			if (std::holds_alternative<T>(result))
			{
				return hold.Wrap(parser->operator()(ctx));
			}
			else
			{
				return NewFailure<F>(std::get<Failure>(result));
			}
		});
	}
	Parser<T> *Or(Parser<T> *parser)
	{
		return ParserFrom<T>([this, parser](Context &ctx) -> Result<T> {
			Holder hold(ctx);
			const auto result = this->operator()(ctx);
			if (std::holds_alternative<T>(result))
			{
				return hold.Wrap(result);
			}
			else
			{
				return hold.Wrap(parser->operator()(ctx));
			}
		});
	}
	template <class F>
	Parser<std::variant<T, F>> *Or(Parser<F> *parser)
	{
		using ReturnType = std::variant<T, F>;

		return ParserFrom<ReturnType>(
			[this, parser](Context &ctx) -> Result<ReturnType> {
				Holder hold(ctx);

				Result<ReturnType> res;
				ReturnType ret;

				const auto thisResult = this->operator()(ctx);
				if (std::holds_alternative<T>(thisResult))
				{
					ret = std::get<T>(thisResult);
					res = ret;
					return hold.Wrap(res);
				}
				else
				{
					const auto thatResult = parser->operator()(ctx);
					if (std::holds_alternative<F>(thatResult))
					{
						ret = std::get<F>(thatResult);
						res = ret;
						return hold.Wrap(res);
					}
				}

				return NewFailure<ReturnType>(std::get<Failure>(thisResult));
			});
	}
	template <class F>
	Parser<F> *ThenReturn(F value)
	{
		return ParserFrom<F>([this, value](Context &ctx) -> Result<F> {
			const auto ret = this->operator()(ctx);
			if (std::holds_alternative<Failure>(ret))
			{
				const auto fail = std::get<Failure>(ret);
				return NewFailure<F>(fail);
			}
			return NewSuccess(value);
		});
	}
	Parser<T> *OrReturn(T value)
	{
		return ParserFrom<T>([this, value](Context &ctx) -> Result<T> {
			const auto ret = this->operator()(ctx);
			if (std::holds_alternative<Failure>(ret))
			{
				return NewSuccess(value);
			}
			return ret;
		});
	}
	template <class F>
	Parser<std::tuple<T, F>> *ThenAlso(Parser<F> *parser)
	{
		using ReturnType = std::tuple<T, F>;

		return ParserFrom<ReturnType>(
			[this, parser](Context &ctx) -> Result<ReturnType> {
				Holder hold(ctx);

				Result<ReturnType> res;
				ReturnType ret;

				const auto firstResult = this->operator()(ctx);
				if (std::holds_alternative<Failure>(firstResult))
				{
					return NewFailure<ReturnType>(std::get<Failure>(firstResult));
				}

				const auto secondResult = parser->operator()(ctx);
				if (std::holds_alternative<Failure>(secondResult))
				{
					return NewFailure<ReturnType>(std::get<Failure>(secondResult));
				}

				std::get<0>(ret) = std::get<T>(firstResult);
				std::get<1>(ret) = std::get<F>(secondResult);

				res = ret;
				return hold.Wrap(res);
			});
	}
	template <class F>
	Parser<T> *Before(Parser<F> *parser)
	{
		return ::Map<T>([](T kind, F) -> T { return kind; }, this, parser);
	}
	template <class F>
	Parser<T> *Between(Parser<F> *parser)
	{
		return ParserFrom<T>([this, parser](Context &ctx) -> Result<T> {
			Holder hold(ctx);
			const auto result = parser->operator()(ctx);
			if (std::holds_alternative<Failure>(result))
			{
				return NewFailure<T>(std::get<Failure>(result));
			}
			const auto result2 = this->operator()(ctx);
			if (std::holds_alternative<Failure>(result2))
			{
				return result2;
			}
			const auto result3 = parser->operator()(ctx);
			if (std::holds_alternative<Failure>(result3))
			{
				return NewFailure<T>(std::get<Failure>(result3));
			}
			return hold.Wrap(result2);
		});
	}
	Parser<QList<T>> *Many()
	{
		return ParserFrom<QList<T>>([this](Context &ctx) -> Result<QList<T>> {
			QList<T> ret;
			bool ok = true;
			do
			{
				auto result = this->operator()(ctx);
				if (std::holds_alternative<Failure>(result))
				{
					ok = false;
				}
				else
				{
					ret << std::get<T>(result);
				}
			} while (ok);
			return NewSuccess(ret);
		});
	}
	Parser<QList<T>> *Repeated(uint n)
	{
		Q_ASSERT(n > 0);
		return ParserFrom<QList<T>>([this, n](Context &ctx) -> Result<QList<T>> {
			Holder hold(ctx);

			QList<T> ret;
			ret.reserve(n);

			for (auto i = 0; i < n; ++i)
			{
				auto result = this->operator()(ctx);
				if (std::holds_alternative<Failure>(result))
				{
					const auto fail = std::get<Failure>(result);
					return NewFailure<QList<T>>(fail);
				}
				ret << std::get<T>(result);
			}

			return hold.Wrap(NewSuccess(ret));
		});
	}
	template <class F>
	Parser<QList<T>> *Until(Parser<F> *terminator)
	{
		return ParserFrom<QList<T>>(
			[this, terminator](Context &ctx) -> Result<QList<T>> {
				Holder hold(ctx);

				QList<T> ret;

				while (true)
				{
					auto result = this->operator()(ctx);
					if (std::holds_alternative<Failure>(result))
					{
						const auto fail = std::get<Failure>(result);
						return NewFailure<QList<T>>(fail);
					}
					else
					{
						ret << std::get<T>(result);
					}
					auto terminatorResult = terminator->operator()(ctx);
					if (std::holds_alternative<Failure>(terminatorResult))
					{
						continue;
					}
					else
					{
						return hold.Wrap(NewSuccess(ret));
					}
				}
			});
	}
	Parser<QString> *ManyString()
	{
		return this->Many()->template Map<QString>(
			[](QStringList strlist) { return strlist.join(""); });
	}
};

template <class Ret, typename Mapper, class... Ts>
Parser<Ret> *Map(Mapper mapper, Parser<Ts> *... parsers)
{
	return Map<Ret, Mapper, Ts...>(mapper, parsers...,
								   std::make_index_sequence<sizeof...(Ts)>());
}

template <class Ret, typename Mapper, class... Ts, size_t... Is>
Parser<Ret> *Map(Mapper mapper, Parser<Ts> *... parsers,
				 std::index_sequence<Is...>)
{
	return ParserFrom<Ret>([=](Context &ctx) -> Result<Ret> {
		Holder hold(ctx);
		std::tuple<Ts...> rets;
		Failure fail;

		(std::visit(
			 [&](auto &&arg) {
				 using T = std::decay_t<decltype(arg)>;
				 if constexpr (std::is_same<T, Failure>::value)
				 {
					 fail = arg;
				 }
				 else
				 {
					 std::get<Is>(rets) = arg;
				 }
			 },
			 parsers->operator()(ctx)),
		 ...);

		if (!fail.expected.isEmpty() || !fail.got.isEmpty())
		{
			return NewFailure<Ret>(fail);
		}
		return hold.Wrap(NewSuccess(std::apply(mapper, rets)));
	});
}

template <class T>
Parser<T>::~Parser<T>() {}

template <class T>
Parser<T> *ParserFrom(std::function<Result<T>(Context &)> parser)
{
	class ParserSub : public Parser<T>
	{
	public:
		std::function<Result<T>(Context &)> fn;
		~ParserSub() override {}
		Result<T> operator()(Context &ctx) override { return fn(ctx); }
	};
	auto ret = new ParserSub;
	ret->fn = parser;
	return ret;
}

auto String(const QString &str) -> Parser<QString> *
{
	return ParserFrom<QString>([str](Context &ctx) -> Result<QString> {
		Holder hold(ctx);
		Result<QString> res;
		const auto read = ctx.Buf.read(str.length());
		if (read.length() < str.length())
		{
			res = Failure{str, QString(read)};
			return res;
		}
		else if (QString(read) != str)
		{
			res = Failure{str, QString(read)};
			return res;
		}
		return hold.Wrap(NewSuccess(QString(read)));
	});
}

auto Strings(const QList<QString> &strs) -> Parser<QString> *
{
	auto copy = strs;
	const auto first = copy[0];
	copy.removeFirst();
	auto parser = String(first);
	for (auto item : copy)
	{
		parser = parser->Or(String(item));
	}
	return parser;
}

auto SkipWhitespace =
	ParserFrom<std::monostate>([](Context &ctx) -> Result<std::monostate> {
		char ch = 0;
		do
		{
			ctx.Buf.read(&ch, 1);
		} while (QChar(ch).isSpace() && !ctx.Buf.atEnd());
		ctx.Buf.seek(ctx.Buf.pos() - 1);
		return NewSuccess<std::monostate>({});
	});

auto ParseToken(std::function<bool(QChar)> fn) -> Parser<QString> *
{
	return ParserFrom<QString>([fn](Context &ctx) -> Result<QString> {
		Holder hold(ctx);
		Result<QString> res;
		char ch = 0;
		const auto read = ctx.Buf.read(&ch, 1);
		if (read == 0)
		{
			res = Failure{"", "<EOF>"};
			return res;
		}
		if (fn(QChar(ch)))
		{
			return hold.Wrap(NewSuccess(QString(ch)));
		}
		else
		{
			res = Failure{"", QString(ch)};
			return res;
		}
	});
}

auto GoIdentifier = Map<QString>(
	[](QString first, QList<QString> trailing) -> QString {
		return first + QStringList(trailing).join("");
	},
	ParseToken([](QChar ch) { return ch.isLetter() || ch == '_'; }),
	ParseToken([](QChar ch) {
		return ch.isLetterOrNumber() || ch == '_';
	})->Many());

auto Any =
	ParserFrom<QChar>([](Context &ctx) -> Result<QChar> {
		char ch = 0;
		const auto read = ctx.Buf.read(1);
		if (read == 0)
		{
			return Result<QChar>(Failure{"", "<EOF>"});
		}
		return Result<QChar>(QChar(ch));
	});
