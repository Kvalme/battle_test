# Игровые механики

[Пошаговый мануал по добавлению юнита, атаки и эффекта](../../docs/04_EXTENSION_GUIDE.md) ·
[Как планировщик вызывает способности и эффекты](../../docs/03_EXECUTION_FLOW.md)

Шаблоны юнитов лежат в `assets/units/`. Порядок способностей в XML задаёт приоритет.

| Юнит | Способности | Реализация |
|---|---|---|
| `swordsman` | `rending`, `melee`, `move` | `Attacks/Rending.cpp`, `Attacks/BaseAttack.cpp`, `Movement/Move.cpp` |
| `hunter` | `poison_arrows`, `ranged`, `melee`, `move` | `Attacks/PoisonArrows.cpp`, `Attacks/BaseAttack.cpp`, `Movement/Move.cpp` |

Фабрики читают параметры из `<ability implementation="..."><properties>…</properties></ability>`. Для значений свойств XML поддерживает типы `int`, `double`, `bool` и `string`. Фабрики проверяют параметры при загрузке шаблона и повторно при создании юнита с переопределёнными характеристиками.

| Ключ фабрики | Обязательные ссылки на целочисленные характеристики | Остальные параметры |
|---|---|---|
| `attack` | `damageStat` | Общие параметры атаки |
| `rending` | `damageStat`, `chanceStat` | Общие параметры атаки |
| `poison_arrows` | `totalDamageStat`, `chanceStat` | Общие параметры атаки |
| `move` | | `cells`, целое от 1 до максимума `unsigned`, по умолчанию 1 |

Обязательный `kind` принимает `melee` или `ranged`. `minRange` по умолчанию равен 1. Для максимальной дальности укажи целочисленный `maxRange` или строковый `maxRangeStat`. По умолчанию используется фиксированный `maxRange=1`. Фиксированный максимум должен быть не меньше минимума. Максимум из характеристики может быть меньше минимума, что оставляет запрос без подходящих целей. `noAdjacent` по умолчанию равен `false`. Тип и длительность эффектов заданы в реализациях `rending` и `poison_arrows`.

Атаки с одинаковым `targetGroup` используют общую цель в пределах активации. `AttackAbility` проверяет совпадение `kind`, `minRange`, `maxRange`, источника `maxRangeStat` и `noAdjacent`. Атаки без группы выбирают цель независимо.
