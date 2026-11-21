#include "Gender.h"

#include <QSet>
#include <QSettings>
#include <QStringList>

namespace Gender {

static QString key(const QString &name)
{
    return name.simplified().toLower();
}

Value byFirstName(const QString &fullName)
{
    // Мужские имена, которые почти не встречаются у актрис (неоднозначные — Alex, Jordan, Charlie — не включены)
    static const QSet<QString> male = [] {
        QSet<QString> s;
        for (const QString &n : QStringLiteral("aaron abel adam adrian ahmed aidan al alan albert alberto alec alejandro alessandro alexander alexei alfie alfonso alfred ali allen alonzo alvin amir anatoly andre andreas andrei andres andrew andy anthony anton antonio archie arnold arthur artur asher august austin axel barry bart ben benjamin benny bernard bill billy blake bobby boris brad bradley brady brandon brendan brent brett brian bruce bruno bryan buck buddy byron caleb calvin carl carlos carter cedric chad charles chester chris christian christopher chuck clark claude clay clint clyde cody colby cole colin connor conor cooper corey craig cristian curtis cyrus dale damian damien damon dan daniel danny dante darius darren dave david dean dennis derek derrick desmond devin dexter diego dillon dimitri dino dmitry dominic don donald donnie donny doug douglas drake duane duke duncan dustin dwayne dylan earl ed eddie edgar eduardo edward edwin eli elias elijah elliot elvis emanuel emilio emmanuel enrico enrique eric erik ernest ernesto ethan eugene evan ezra fabio felipe felix fernando filip francesco francis francisco frank frankie franz fred freddie freddy frederick gabriel gareth garrett gary gavin gene george gerald gerard gianni gilbert giovanni glenn gordon graham grant greg gregory guido gustavo guy hank hans harold harrison harry harvey hector henry herbert herman hugo hunter ian igor isaac ivan jack jackson jacob jake james jared jason jasper javier jay jeff jeffrey jeremy jerome jerry jesus jim jimmy joe joel joey johan john johnny jon jonah jonathan jordi jorge jose joseph josh joshua juan julian julio justin karl keiran keith ken kenny kevin kieran kirk kurt kyle lance larry lars leo leon leonard leonardo leroy levi lewis liam lincoln logan lorenzo louie louis lucas luis luke malcolm manuel marc marcel marco marcus mario mark marshall martin marty mason mathias matt matteo matthew maurice max maxim maximilian maxwell mick mickey miguel mike mikhail miles milo mitch mitchell mohamed moses nate nathan nathaniel neil nelson nicholas nick nico nicolas nigel noah noel norman oleg oliver omar oscar otto owen pablo paolo patrick paul pavel pedro pete peter phil philip philippe pierre preston quentin rafael ralph ramon randy raoul raul ray raymond reed reggie rex ricardo richard richie rick ricky rob robert roberto rocco rocky rod rodney rodrigo roger roman romeo ron ronald ronnie ross roy ruben rudy russell ryan samuel santiago scott sean sebastian seth shane shaun shawn sid simon sonny spencer stan stanley stefan stephen steve steven stuart sven ted teddy terrence theo theodore thomas tim timothy toby todd tom tommy tony travis trent trevor troy tyler tyrone valentino victor vince vincent vinnie vlad vladimir wade walter warren wayne wes wesley will william willie xander xavier zac zach zachary zack zane").split(' ', Qt::SkipEmptyParts))
            s.insert(n);
        return s;
    }();
    const QString first = key(fullName).section(' ', 0, 0);
    return male.contains(first) ? Male : Unknown;
}

Value known(const QString &fullName, Source *src)
{
    QSettings st;
    // Старый список мужчин (ручные отметки из прошлых версий)
    if (st.value(QStringLiteral("ae/men")).toStringList().contains(fullName.simplified())) {
        if (src) *src = ByUser;
        return Male;
    }
    const QString v = st.value(QStringLiteral("aeGender/") + key(fullName)).toString();
    if (v.isEmpty()) {
        if (src) *src = None;
        return Unknown;
    }
    if (src) *src = v.endsWith(QLatin1Char('u')) ? ByUser : ByDatabase;
    return v.startsWith(QLatin1Char('m')) ? Male : Female;
}

void remember(const QString &fullName, Value v, Source src)
{
    QSettings st;
    // Отметку пользователя не перезаписываем данными из базы
    Source old = None;
    known(fullName, &old);
    if (old == ByUser && src != ByUser)
        return;
    QStringList men = st.value(QStringLiteral("ae/men")).toStringList();
    if (men.removeAll(fullName.simplified()))
        st.setValue(QStringLiteral("ae/men"), men);
    if (v == Unknown) {
        st.remove(QStringLiteral("aeGender/") + key(fullName));
        return;
    }
    st.setValue(QStringLiteral("aeGender/") + key(fullName),
                QString(v == Male ? QLatin1Char('m') : QLatin1Char('f')) + (src == ByUser ? QStringLiteral("u") : QStringLiteral("d")));
}

} // namespace Gender
