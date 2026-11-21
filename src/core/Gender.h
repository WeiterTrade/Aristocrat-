#pragma once
#include <QString>

// Пол исполнителя: мужчин не пишем в NFO и не качаем их фото.
// Источники по старшинству: ручная отметка пользователя → проверка по IAFD (запоминается) → мужское имя.
namespace Gender {
enum Value { Unknown = 0, Female = 1, Male = 2 };
enum Source { None = 0, ByName = 1, ByDatabase = 2, ByUser = 3 };

Value byFirstName(const QString &fullName);           // по имени: «John Strong» → Male
Value known(const QString &fullName, Source *src);    // запомненное (пользователь или IAFD)
void remember(const QString &fullName, Value v, Source src);
}
