#include <QDebug>
#include <QString>

struct Statement
{
	QString lhs;
	QString rhs;
};

QDebug operator<<(QDebug debug, const Statement &data)
{
	QDebugStateSaver saver(debug);
	debug.nospace() << "Statement('" << data.lhs << "','" << data.rhs << "')";
	return debug;
}

#include "parser.h"

auto colon = String(":");
auto stringUntil = [](QChar ch) -> Parser<QString>* {
	return ParseToken([ch](QChar comp) -> bool {
		return ch != comp;
	});
};
auto untilColon = stringUntil(':');
auto untilNewlines = stringUntil(':');
auto statement = untilColon->Before(untilColon)->ThenAlso(untilNewlines)->Map<Statement>([](std::tuple<QString, QString> tuple) -> Statement {
	auto [lhs, rhs] = tuple;
	return Statement {
		.lhs = lhs,
		.rhs = rhs
	};
});

int main(int argc, char *argv[])
{
	qDebug() << statement->Parse("lhs: rhs\n");
	return 0;
}
