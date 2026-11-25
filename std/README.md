# Стандартная библиотека PGT

Пакеты подключаются по имени: `#use "strings"`. Исходники написаны на PGT и лежат рядом с этим файлом;
низкоуровневые части (строки, файлы, время) реализованы в рантайме `runtime/pgt_runtime.c` и подключаются через `extern fn`.

Каталог библиотеки ищется так: переменная окружения `PGT_STD`, затем `../lib/pgt/std` рядом с компилятором, затем `std/` в исходниках.

## runtime и builtin

`runtime` подключается к каждой программе автоматически, `#use` не нужен. Он реализует `panic` и проверки границ, `nil` и деления на ноль (`panicString`, `panicIndex`, `panicNil`, `panicDiv`; все функции закрытые, их вызывает только компилятор). Сообщения и код выхода 2 не зависят от того, откуда вызвана паника.

`builtin` только описывает встроенные функции (`std/builtin/builtin.pgt`), как `builtin.go` в Go. Он никогда не компилируется, и `#use "builtin"` даёт ошибку.

## fmt

Вывод и ввод в консоль. Написан на PGT поверх типа `any` и пакета `reflect`; встроенных `print`/`println`/`input` в языке нет.

```pgt
#use "fmt"

fmt.Println("x=", 1, " y=", 2.5)                 // x=1 y=2.5
fmt.Printf("%5.2f|%-4d|%q\n", 3.14159, 7, "hi")   //  3.14|7   |"hi"
var* n = 0
fmt.Input("n? ", &n)
```

| Функция | Описание |
|---|---|
| `Print(args ...any)` / `Println(args ...any)` | Печатает аргументы без пробелов между ними (`Println` добавляет перевод строки) |
| `Sprint(args ...any) string` / `Sprintln(args ...any) string` | То же, но возвращает строку |
| `Printf(format string, args ...any)` / `Sprintf(format string, args ...any) string` | Форматирование: `%v %d %s %f %g %c %t %q %x %X %%`, флаги `- 0 +`, ширина и точность |
| `Eprint(args ...any)` / `Eprintln(args ...any)` / `Eprintf(format string, args ...any)` | То же, но в stderr (перед записью stdout сбрасывается, чтобы порядок строк сохранялся) |
| `Input(prompt string, p any)` | Печатает подсказку и читает значение по указателю (`int`, `int64`, `float`, `char`, `bool`, слово в `string`); при неудаче значение не меняется |
| `InputLine(prompt string, p any)` | Печатает подсказку и читает строку целиком в `*string` |
| `Scan(args ...any) int` | Читает слова, разделённые пробелами, в указатели; возвращает число прочитанных |
| `Scanln(args ...any) int` | Разбирает остаток текущей строки по словам |
| `ReadLine() string` | Читает следующую строку без `\n` |

## strings

Работа со строками. Строки неизменяемы: функции возвращают новую строку.

```pgt
#use "strings"
#use "fmt"

var* words = strings.Fields("  to be  or not ")   // [to be or not]
fmt.Println(strings.Join(words, "-"))                 // to-be-or-not
fmt.Println(strings.PadLeft("7", 3, '0'))             // 007

var* b strings.Builder
b.Write("Hello, ")
b.Write("PGT")
fmt.Println(b.String())
```

| Функция | Описание |
|---|---|
| `Contains(s string, sub string) bool` | Есть ли sub в s |
| `ContainsChar(s string, c char) bool` | Есть ли символ c в s |
| `HasPrefix(s string, prefix string) bool` | Начинается ли s с prefix |
| `HasSuffix(s string, suffix string) bool` | Заканчивается ли s на suffix |
| `Index(s string, sub string) int` | Позиция первого вхождения sub или -1 |
| `IndexFrom(s string, sub string, from int) int` | Ищет sub, начиная с позиции from |
| `LastIndex(s string, sub string) int` | Позиция последнего вхождения sub или -1 |
| `IndexChar(s string, c char) int` | Позиция первого символа c или -1 |
| `Count(s string, sub string) int` | Возвращает число непересекающихся вхождений sub в s |
| `Upper(s string) string` | Все буквы заглавные |
| `Lower(s string) string` | Все буквы строчные |
| `Title(s string) string` | Делает заглавной первую букву каждого слова |
| `Repeat(s string, n int) string` | S, повторённая n раз |
| `Replace(s string, old string, with string) string` | Заменяет все вхождения old на with |
| `ReplaceN(s string, old string, with string, n int) string` | Заменяет не больше n первых вхождений |
| `Reverse(s string) string` | Строка задом наперёд |
| `EqualFold(a string, b string) bool` | Сравнивает строки без учёта регистра |
| `TrimSpace(s string) string` | Убирает пробелы, табуляции и переводы строк с обоих концов |
| `TrimLeft(s string) string` | Убирает пробельные символы слева |
| `TrimRight(s string) string` | Убирает пробельные символы справа |
| `Trim(s string, cutset string) string` | Убирает с обоих концов все символы, входящие в cutset |
| `TrimPrefix(s string, prefix string) string` | Убирает prefix в начале, если он есть |
| `TrimSuffix(s string, suffix string) string` | Убирает suffix в конце, если он есть |
| `PadLeft(s string, width int, fill char) string` | Дополняет слева символом fill до ширины width |
| `PadRight(s string, width int, fill char) string` | Дополняет справа символом fill до ширины width |
| `Center(s string, width int, fill char) string` | Выравнивает по центру в поле ширины width |
| `Split(s string, sep string) []string` | Делит s по разделителю sep. Пустой sep делит строку на отдельные символы |
| `Fields(s string) []string` | Делит строку по пробельным символам, пропуская пустые части |
| `Lines(s string) []string` | Делит текст на строки (понимает и `\n`, и `\r\n`) |
| `Join(parts []string, sep string) string` | Склеивает части через sep |
| `IsDigit(c char) bool` | Цифра 0–9 |
| `IsLetter(c char) bool` | Латинская буква |
| `IsUpper(c char) bool` | Заглавная латинская буква |
| `IsLower(c char) bool` | Строчная латинская буква |
| `IsAlnum(c char) bool` | Буква или цифра |
| `IsSpace(c char) bool` | Пробел, табуляция или перевод строки |
| `ToUpper(c char) char` | Символ в верхнем регистре |
| `ToLower(c char) char` | Символ в нижнем регистре |
| `struct Builder` | Собирает строку по частям: `var* b strings.Builder` |
| `(Builder) Write(s string)` | Добавляет строку |
| `(Builder) WriteChar(c char)` | Добавляет символ |
| `(Builder) WriteLine(s string)` | Добавляет строку и `\n` |
| `(Builder) Len() int` | Длина собранной строки |
| `(Builder) Reset()` | Очищает |
| `(Builder) String() string` | Готовая строка |

## math

Математика. `abs`, `min`, `max`, `sqrt`, `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp` встроены в язык, здесь всё остальное.

```pgt
#use "math"
#use "fmt"

fmt.Println(math.Pi, " ", math.Hypot(3, 4))    // 3.14159 5
fmt.Println(math.Gcd(84, 36), " ", math.IsPrime(97))
```

Константы: `Pi`, `E`, `Sqrt2`, `Ln2`, `Ln10`, `MaxInt`, `MinInt`, `MaxInt64`, `MinInt64`.

| Функция | Описание |
|---|---|
| `Sqrt(x float) float` | Квадратный корень |
| `Cbrt(x float) float` | Кубический корень |
| `Pow(x float, y float) float` | X в степени y |
| `Exp(x float) float` | E в степени x |
| `Log(x float) float` | Натуральный логарифм |
| `Log2(x float) float` | Логарифм по основанию 2 |
| `Log10(x float) float` | Десятичный логарифм |
| `Hypot(x float, y float) float` | Длина гипотенузы √(x²+y²) |
| `Sin(x float) float` | Синус (радианы) |
| `Cos(x float) float` | Косинус |
| `Tan(x float) float` | Тангенс |
| `Asin(x float) float` | Арксинус |
| `Acos(x float) float` | Арккосинус |
| `Atan(x float) float` | Арктангенс |
| `Atan2(y float, x float) float` | Угол точки (x, y) в радианах |
| `Sinh(x float) float` | Гиперболический синус |
| `Cosh(x float) float` | Гиперболический косинус |
| `Tanh(x float) float` | Гиперболический тангенс |
| `Floor(x float) float` | Округление вниз |
| `Ceil(x float) float` | Округление вверх |
| `Round(x float) float` | Округление к ближайшему (половина — от нуля) |
| `Trunc(x float) float` | Отбрасывает дробную часть |
| `Abs(x float) float` | Модуль |
| `Mod(x float, y float) float` | Остаток от деления вещественных чисел (знак как у x) |
| `Min(a float, b float) float` | Меньшее из двух |
| `Max(a float, b float) float` | Большее из двух |
| `Clamp(x float, lo float, hi float) float` | Ограничивает x отрезком [lo, hi] |
| `Lerp(a float, b float, t float) float` | Линейная интерполяция между a и b |
| `Radians(deg float) float` | Градусы в радианы |
| `Degrees(rad float) float` | Радианы в градусы |
| `Inf() float` | Бесконечность |
| `NaN() float` | «не число» |
| `IsNaN(x float) bool` | Является ли x NaN |
| `Float64bits(f float) int64` | Битовое представление IEEE 754 |
| `Float64frombits(b int64) float` | Число по битовому представлению |
| `Signbit(x float) bool` | Установлен ли знаковый бит (в том числе у `-0.0`) |
| `IsInf(x float) bool` | Является ли x бесконечностью |
| `Sign(x float) int` | Возвращает -1, 0 или 1 |
| `ClampInt(x int, lo int, hi int) int` | Ограничивает целое отрезком [lo, hi] |
| `SignInt(x int) int` | Знак целого: -1, 0 или 1 |
| `PowInt(base int64, exp int) int64` | Возводит целое в неотрицательную степень (быстрое возведение) |
| `Gcd(a int, b int) int` | Наибольший общий делитель |
| `Lcm(a int, b int) int` | Наименьшее общее кратное |
| `IsPrime(n int) bool` | Простое ли число |
| `Factorial(n int) int64` | N! (до 20! включительно) |
| `Fib(n int) int64` | N-е число Фибоначчи (до 92) |
| `IsEven(n int) bool` | Чётное ли число |
| `IsOdd(n int) bool` | Нечётное ли число |
| `DigitSum(n int) int` | Сумма цифр числа |

## arrays

Функции для массивов. Массив передаётся по ссылке на данные, поэтому `Sort`, `Reverse` и `Fill` меняют сам массив.

```pgt
#use "arrays"
#use "fmt"

var* a = [5, 3, 9, 1]
arrays.Sort(a)                          // [1 3 5 9]
fmt.Println(arrays.BinarySearch(a, 5))      // 2
```

| Функция | Описание |
|---|---|
| `Sum(a []int) int` | Сумма элементов |
| `Min(a []int) int` | Наименьший элемент (паника на пустом массиве) |
| `Max(a []int) int` | Наибольший элемент (паника на пустом массиве) |
| `Mean(a []int) float` | Среднее арифметическое |
| `Contains(a []int, v int) bool` | Есть ли v в массиве |
| `IndexOf(a []int, v int) int` | Индекс первого v или -1 |
| `Count(a []int, v int) int` | Сколько раз встречается v |
| `Equal(a []int, b []int) bool` | Одинаковы ли массивы поэлементно |
| `Copy(a []int) []int` | Независимая копия |
| `Slice(a []int, from int, to int) []int` | Возвращает копию элементов [from, to) |
| `Concat(a []int, b []int) []int` | Новый массив: a, затем b |
| `Range(from int, to int) []int` | Возвращает [from, from+1, ..., to-1] |
| `Repeat(v int, n int) []int` | Массив из n значений v |
| `Fill(a []int, v int)` | Заполняет массив значением v |
| `Reverse(a []int)` | Переворачивает массив на месте |
| `IsSorted(a []int) bool` | Отсортирован ли по возрастанию |
| `Sort(a []int)` | Сортирует массив по возрастанию (быстрая сортировка + вставки для малых кусков) |
| `SortDesc(a []int)` | Сортирует по убыванию |
| `BinarySearch(a []int, v int) int` | Ищет v в отсортированном массиве; -1, если не найден |
| `Unique(a []int) []int` | Возвращает элементы без повторов, сохраняя порядок первого появления |
| `SumFloat(a []float) float` | Сумма элементов |
| `MeanFloat(a []float) float` | Среднее арифметическое |
| `SortFloat(a []float)` | Сортирует по возрастанию |
| `ContainsString(a []string, v string) bool` | Есть ли строка v в массиве |
| `IndexOfString(a []string, v string) int` | Индекс первой строки v или -1 |
| `ReverseStrings(a []string)` | Переворачивает массив на месте |
| `SortStrings(a []string)` | Сортирует строки лексикографически (сортировка Шелла) |

## conv

Преобразования между строками и числами.

```pgt
#use "conv"
#use "fmt"

fmt.Println(conv.ToHex(255), " ", conv.FormatFloat(2.0 / 3, 2))   // ff 0.67
if conv.IsInt(text) { var* n = conv.Atoi(text) }
```

| Функция | Описание |
|---|---|
| `Itoa(n int) string` | Число в строку |
| `Atoi(s string) int` | Строку в число (0, если не число) |
| `ParseInt64(s string) int64` | Строку в int64 |
| `ParseFloat(s string) float` | Строку в float |
| `ParseBool(s string) bool` | Понимает true/false, 1/0, yes/no |
| `IsInt(s string) bool` | Записано ли в строке целое число |
| `IsFloat(s string) bool` | Записано ли в строке число |
| `FormatFloat(x float, prec int) string` | Печатает число с фиксированным числом знаков после точки |
| `FormatInt(n int64, base int) string` | Записывает число в системе счисления base (2..36) |
| `ToBinary(n int64) string` | Двоичная запись |
| `ToHex(n int64) string` | Шестнадцатеричная запись |
| `ParseIntBase(s string, base int) int64` | Читает число в системе счисления base; неизвестные символы пропускаются |

## io

Построчное чтение стандартного ввода.

```pgt
#use "io"
#use "fmt"

var* line = io.ReadLine()
while !io.EOF() {
    fmt.Println("> ", line)
    line = io.ReadLine()
}
```

| Функция | Описание |
|---|---|
| `ReadLine() string` | Читает следующую строку (без '\n'). В конце ввода возвращает "" и EOF() становится true |
| `EOF() bool` | Закончился ли ввод при последнем чтении |
| `ReadLines() []string` | Читает все оставшиеся строки |
| `Prompt(text string) string` | Печатает подсказку и читает строку целиком |
| `ReadInt(text string) int` | Печатает подсказку и читает целое |
| `ReadFloat(text string) float` | Печатает подсказку и читает число |

## os

Аргументы командной строки, окружение, файлы и запуск команд.

```pgt
#use "os"
#use "fmt"

for arg in os.Args() { fmt.Println(arg) }
os.WriteFile("out.txt", "hello\n")
```

| Функция | Описание |
|---|---|
| `Args() []string` | Возвращает аргументы программы; Args()[0] — имя самой программы |
| `NumArgs() int` | Число аргументов без имени программы |
| `Arg(i int) string` | Возвращает i-й аргумент (начиная с 1) или "", если его нет |
| `Getenv(name string) string` | Значение переменной окружения или "" |
| `Run(cmd string) int` | Запускает команду оболочки и возвращает её код завершения |
| `Exit(code int)` | Завершает программу с кодом code |
| `ReadFile(path string) string` | Содержимое файла или "", если его нет |
| `WriteFile(path string, data string) bool` | Записывает файл заново |
| `AppendFile(path string, data string) bool` | Дописывает в конец файла |
| `Exists(path string) bool` | Существует ли файл или папка |
| `IsDir(path string) bool` | Является ли путь папкой |
| `Remove(path string) bool` | Удаляет файл |
| `ReadLines(path string) []string` | Читает файл и делит его на строки |
| `struct DirEntry` | Запись папки: поля `Name string` и `IsDir bool` |
| `ReadDir(dir string) []DirEntry` | Содержимое папки без `.` и `..`, отсортированное по имени (побайтово); для несуществующей папки пустой массив |
| `Mkdir(path string) bool` | Создаёт одну папку (права 0777 с учётом umask) |
| `MkdirAll(path string) bool` | Создаёт папку вместе с родителями; `true`, если она уже есть |
| `Rmdir(path string) bool` | Удаляет пустую папку |
| `Getwd() string` | Текущая рабочая папка |

## path

Работа с путями в стиле Go (`path`, разделитель `/`). Только строки: файловую систему не трогает, кроме `Getwd` и `Abs`.

```pgt
#use "path"
#use "fmt"

fmt.Println(path.Join("a", "b/../c"))     // a/c
fmt.Println(path.Base("/x/y.txt"), path.Ext("y.txt"))
var* s = path.Split("/usr/lib/libc.so")   // s.Dir = "/usr/lib/", s.File = "libc.so"
```

| Функция | Описание |
|---|---|
| `Join(parts ...string) string` | Склеивает части через `/` и очищает результат; пустые части пропускаются, результат пуст, если все части пусты |
| `Clean(p string) string` | Убирает `.`, повторные `/` и `..` (как `path.Clean` в Go); пустой путь даёт `.` |
| `Split(p string) SplitResult` | Делит по последнему `/`: `Dir` (с `/` на конце) и `File` |
| `Dir(p string) string` | Всё, кроме последнего элемента, очищенное |
| `Base(p string) string` | Последний элемент без завершающих `/`; для пустого пути `.`, для `/` — `/` |
| `Ext(p string) string` | Расширение последнего элемента вместе с точкой или "" |
| `IsAbs(p string) bool` | Начинается ли путь с `/` |
| `Getwd() string` | Текущая рабочая папка |
| `Abs(p string) string` | Абсолютный очищенный путь |
| `Rel(base string, target string) string` | Относительный путь от base к target или "", если он не существует |

## flag

Разбор аргументов командной строки без функций-значений. `FlagSet` создаёт флаги и возвращает указатели, в которые `Parse` записывает значения.

```pgt
#use "flag"
#use "fmt"

var* name = flag.String("name", "world", "who to greet")
var* n = flag.Int("n", 1, "repeat count")
var* v = flag.Bool("v", false, "verbose")

fn main() {
    var* err = flag.Parse()
    if err != "" {
        fmt.Println(err)
        return
    }
    fmt.Println(*name, *n, *v, flag.Args())
}
```

Поддерживаются `-name value`, `-name=value`, `--name`, булевы `-v`, `-v=false`, разделитель `--`. Разбор останавливается на первом аргументе без `-` (одиночный `-` тоже позиционный). Ошибка возвращается строкой, пустая строка означает успех.

| Функция | Описание |
|---|---|
| `NewFlagSet(name string) *FlagSet` | Новый набор флагов |
| `(fs) String(name string, def string, usage string) *string` | Строковый флаг |
| `(fs) Int(name string, def int, usage string) *int` | Целочисленный флаг (32 бита) |
| `(fs) Bool(name string, def bool, usage string) *bool` | Булев флаг |
| `(fs) Parse(args []string) string` | Разбирает args (без имени программы), возвращает текст ошибки или "" |
| `(fs) Args() []string`, `NArg() int`, `Arg(i int) string` | Позиционные аргументы |
| `(fs) IsSet(name string) bool`, `Parsed() bool` | Был ли флаг задан, был ли вызван `Parse` |
| `(fs) Usage() string` | Справка: флаги по алфавиту, значения по умолчанию |
| `String`, `Int`, `Bool`, `Parse()`, `Args`, `NArg`, `Arg`, `IsSet`, `Parsed`, `Usage` | То же для набора `CommandLine`; `Parse()` берёт `os.Args()` без имени программы |

Повторное объявление флага — `panic: flag redefined: имя`.

## sort

Сортировка через интерфейс `Sorter`, как `sort.Interface` в Go.

```pgt
#use "sort"

interface Sorter {
    Len() int
    Less(i int, j int) bool
    Swap(i int, j int)
}

var* a = [5, 2, 9]
sort.Ints(a)
```

| Функция | Описание |
|---|---|
| `Sort(data Sorter)` | Быстрая сортировка с переключением на пирамидальную: O(n log n) в худшем случае, не стабильная |
| `Stable(data Sorter)` | Стабильная сортировка (вставки по блокам и слияние на месте через `Swap`), O(n log² n) |
| `IsSorted(data Sorter) bool` | Упорядочены ли элементы |
| `Ints([]int)`, `Int64s([]int64)`, `Float64s([]float)`, `Strings([]string)` | Сортировка по возрастанию (`NaN` идут первыми) |
| `IntsAreSorted([]int) bool`, `StringsAreSorted([]string) bool` | Проверка порядка |
| `SearchInts(a []int, x int) int`, `SearchInt64s`, `SearchStrings` | Бинарный поиск: наименьший индекс `i`, где `a[i] >= x` (или `len(a)`) |
| `IntSlice`, `Int64Slice`, `Float64Slice`, `StringSlice` | Обёртки `{A: массив}`, реализующие `Sorter` |

## time

Время и паузы. Моменты времени — секунды или миллисекунды Unix (`int64`).

```pgt
#use "time"
#use "fmt"

var* sw = time.StartStopwatch()
time.Sleep(100)
fmt.Println(sw.ElapsedMs(), " ms; ", time.Format(time.Now(), time.DateTime))
```

Константы: `DateTime`, `Date`, `Clock`.

| Функция | Описание |
|---|---|
| `Now() int64` | Текущее время в секундах Unix |
| `NowMs() int64` | Текущее время в миллисекундах Unix |
| `Ticks() int64` | Монотонные миллисекунды: годятся для замеров, не зависят от смены часов |
| `Since(startTicks int64) int64` | Сколько миллисекунд прошло с момента Ticks() |
| `Sleep(ms int)` | Пауза в миллисекундах |
| `Format(seconds int64, layout string) string` | Форматирует время по шаблону strftime: time.Format(time.Now(), time.DateTime) |
| `Today() string` | Сегодняшняя дата в виде ГГГГ-ММ-ДД |
| `struct Stopwatch` | Секундомер |
| `StartStopwatch() Stopwatch` | Запускает секундомер |
| `(Stopwatch) ElapsedMs() int64` | Миллисекунды с запуска |
| `(Stopwatch) Restart()` | Начинает отсчёт заново |

## rand

Псевдослучайные числа (xorshift64*). Без `Seed` генератор стартует от текущего времени.

```pgt
#use "rand"
#use "fmt"

fmt.Println(rand.Range(1, 6))       // бросок кубика
rand.Seed(42)                   // одна и та же последовательность при каждом запуске
```

| Функция | Описание |
|---|---|
| `Seed(seed int64)` | Задаёт начальное значение: одинаковый seed — одинаковые числа |
| `Int() int` | Возвращает неотрицательное случайное int |
| `IntN(n int) int` | Возвращает число от 0 до n-1 |
| `Range(lo int, hi int) int` | Возвращает число от lo до hi включительно |
| `Float() float` | Возвращает число из [0, 1) |
| `Bool() bool` | Случайный bool |
| `Chance(p float) bool` | Возвращает true с вероятностью p (от 0 до 1) |
| `Shuffle(a []int)` | Перемешивает массив |
| `ShuffleStrings(a []string)` | Перемешивает массив строк |
| `Pick(a []string) string` | Случайный элемент массива |

## memory

Блоки памяти для переменных `var&` (`var& := buf = malloc(free[byte] n)`, освобождение — `free(buf)`). Подключается как `#use "pgt/std/memory"` (также работает `"memory"`).

```pgt
#use "pgt/std/memory"
#use "fmt"

var& := buf = malloc(free[byte] memory.KB(4))
memory.Set(buf, 7)
var* head = [byte(1), byte(2)]
fmt.Println(memory.Copy(buf, head), " ", buf[0], " ", buf[2])   // 2 1 7
free(buf)
```

Сама выделение и освобождение для `var&` написаны на PGT в этом пакете (поверх `calloc`/`free` из libc); компилятор только разбирает синтаксис и вызывает внутренние функции пакета. Те же блоки можно получать напрямую как `[]byte`.

```pgt
#use "fmt"

var* buf = memory.Malloc(4)
buf = memory.Grow(buf, 4)          // 8 байт, старые данные сохранены, новые нули
fmt.Println(memory.Live(), " ", memory.LiveBytes())   // 1 8
memory.Free(buf)                   // Live() == 0
```

`Malloc`/`Realloc`/`Grow` возвращают блок, который компилятор не отслеживает: освобождайте его через `memory.Free`. После `Realloc` и `Grow` старый массив недействителен, используйте возвращённый.

| Функция | Описание |
|---|---|
| `Malloc(n int) []byte` | Новый блок из n нулевых байт. `n < 0` — `panic: malloc: negative size` |
| `Free(buf []byte)` | Освобождает блок из `Malloc`/`Realloc`/`Grow`; пустой буфер игнорируется |
| `Realloc(buf []byte, n int) []byte` | Меняет размер блока до n байт, сохраняя содержимое; новые байты нулевые. `n < 0` — `panic: realloc: negative size` |
| `Grow(buf []byte, extra int) []byte` | `Realloc(buf, len(buf) + extra)`. `extra < 0` — `panic: grow: negative size` |
| `Copy(dst []byte, src []byte) int` | Копирует min(len(dst), len(src)) байт из src в dst (области могут пересекаться), возвращает их число |
| `Set(buf []byte, v byte)` | Записывает v во все байты buf |
| `Zero(buf []byte)` | Обнуляет buf |
| `Fill(buf []byte, pattern []byte)` | Заполняет buf, циклически повторяя pattern |
| `Compare(a []byte, b []byte) int` | Лексикографическое сравнение: -1, 0 или 1 |
| `Equal(a []byte, b []byte) bool` | Равны ли содержимое и длина |
| `Live() int` | Число живых ручных блоков (`var&`, `Malloc`, `Realloc`) |
| `LiveBytes() int64` | Суммарный размер живых ручных блоков в байтах |
| `KB(n int) int`, `MB(n int) int` | n килобайт и n мегабайт |
| `GB(n int) int64` | n гигабайт |

## unsafe

Сырая память и доступ к значениям в `any`. Подключается как `#use "unsafe"`. Основа для `reflect`; подробности и правило учёта ссылок — в `syntax.md`.

| Функция | Описание |
|---|---|
| `Add(p *byte, n int64) *byte` | Смещение указателя в байтах |
| `Alloc(size int64, drop *byte) *byte` | Управляемый блок с функцией освобождения |
| `Copy(dst, src *byte, n int64)` | Копирование байтов |
| `LoadInt/LoadInt64/LoadFloat/LoadChar/LoadByte/LoadBool/LoadString/LoadPtr(p *byte)` | Чтение значения по адресу |
| `StoreInt/StoreInt64/StoreFloat/StoreChar/StoreByte/StoreBool/StoreString/StorePtr(p *byte, v)` | Запись значения по адресу (строки и указатели удерживаются) |
| `TypeOf(a any) *byte`, `DataOf(a any) *byte`, `MakeAny(data, desc *byte) any` | Описатель типа и данные значения `any` |
| `Sizeof(a any) int64`, `Addr(p *byte) int64` | Размер типа, числовой адрес |
| `StringAt`, `StringData`, `BytesAt`, `BytesData`, `CallPtr` | Приведения строк и массивов, вызов функции по указателю |

## reflect

Рефлексия над `any`, написана на PGT поверх `unsafe`. Подключается как `#use "reflect"`.

| Функция | Описание |
|---|---|
| `TypeOf(a any) *Type`, `Kind(a any) int`, `TypeName(a any) string` | Описатель типа, вид (`KindInt`, `KindStruct`, ...), имя |
| `Int`, `Float`, `Bool`, `String` | Значение базового типа (`Int` принимает int, int64, char, byte; `Uint` — uint64) |
| `Len(a)`, `Index(a, i) any` | Длина и элемент массива |
| `NumField(a)`, `FieldName(a, i)`, `Field(a, i) any` | Поля структуры |
| `Elem(a) any`, `IsNil(a)`, `Addr(a) int64`, `SlotIface(a, i)` | Указатели и интерфейсные слоты |
| `SetInt`, `SetFloat`, `SetBool`, `SetChar`, `SetString` | Запись через указатель, лежащий в `any` |

## maps

Хеш-таблица для типа `map[K]V` (временный синтаксис, см. `syntax.md`). Написана на PGT поверх `unsafe` и `reflect`, загружается неявно, `#use "maps"` не нужен. Функции принимают адреса ключа и значения и предназначены для компилятора и `fmt`; `Keys`/`Values` без обобщённых типов не реализуемы.

| Функция | Описание |
|---|---|
| `New(keyDesc, valDesc, dropFn *byte) *byte` | Создаёт таблицу |
| `Get(m, k, out *byte)`, `Set(m, k, v *byte)` | Чтение (при отсутствии ключа `out` не меняется) и запись; запись в `nil` — паника |
| `Has(m, k *byte) bool`, `Delete(m, k *byte)`, `Len(m *byte) int` | Наличие, удаление, размер |
| `Next(m *byte, i int64) int64`, `KeyAt`, `ValAt` | Обход в порядке вставки |
| `KeyType(m)`, `ValType(m)`, `Drop(m)` | Дескрипторы типов и освобождение |

## asm

Ассемблер x86-64 в синтаксисе Intel/NASM, целиком на PGT (`std/asm`, пакет из нескольких файлов: лексер, разбор операндов, кодирование, метки, загрузка в исполняемую память). Переводит текст в машинный код.

```pgt
#use "asm"
#use "fmt"

var* code = asm.MustAssemble("mov rax, rdi\nadd rax, rsi\nret")
fmt.Println(asm.Hex(code))
var* f = asm.Load(code)
fmt.Println(f.Call(40, 2))
asm.Free(f)
```

Синтаксис: одна инструкция на строку, `;` начинает комментарий, метки `name:`, числа десятичные, `0x..`, `0b..` и отрицательные, регистры и мнемоники без учёта регистра. Память: `[base]`, `[base + index*scale + disp]`, `[rip + label]`, `[0x1000]`, размер `byte`/`word`/`dword`/`qword` (с необязательным `ptr`) перед `[`.

Регистры: `rax..r15`, `eax..r15d`, `ax..r15w`, `al..dil`, `r8b..r15b`, `ah ch dh bh` (с REX недопустимы, будет ошибка).

Инструкции: `mov movzx movsx movsxd lea add sub adc sbb and or xor cmp test not neg inc dec imul mul idiv div cqo cdq shl shr sar rol ror push pop xchg jmp call jcc setcc cmovcc ret nop int3 int syscall rdtsc cpuid pause hlt leave`. Условия: `e ne z nz l le g ge b be a ae s ns o no p np c nc` и синонимы (`nge`, `nle`, `na`, ...). Выбираются кратчайшие формы (imm8 или imm32, короткие формы для аккумулятора, `mov r64, imm32` через `C7`, `movabs` для 64-битных констант). Переходы по меткам используют `rel8`, если помещаются, иначе `rel32` (релаксация до неподвижной точки); `call label` всегда `rel32`.

| Функция | Описание |
|---|---|
| `Assemble(src string) Result` | Ассемблирует текст; `Result{Code []byte, Err string, Line int}`, методы `Ok()` и `Error()` |
| `MustAssemble(src string) []byte` | То же, при ошибке паника `asm: line N: сообщение` |
| `Hex(code []byte) string` | Байты в виде `48 c7 c0 0a 00 00 00` |
| `Load(code []byte) Func` | Копирует код в страницу `mmap` и ставит `PROT_READ\|PROT_EXEC` (записываемой и исполняемой одновременно она не бывает) |
| `f.Call(args ...int64) int64` | Вызывает код как функцию System V (до 6 целочисленных аргументов, результат в `rax`) |
| `f.Addr() int64` | Адрес кода (для косвенных вызовов из другого кода) |
| `Free(f Func)` | `munmap` |
| `CheckBlock(text string, names []string, sizes []int) Result` | Проверяет текст блока `asm { }` без запуска: подстановки `{имя}`, запрет `r15`, `ret`, `leave`, синтаксис; `Result.Line` — строка внутри блока |
| `RunBlock(cache *byte, text string, names []string, sizes []int, table *byte, where string, line int)` | Служебная функция старой схемы запуска блоков во время выполнения; компилятор её больше не вызывает (блоки `asm { }` компилируются в программу как встроенный ассемблер LLVM), но она остаётся в пакете |

Блоки `asm { }` в самом языке описаны в `syntax.md` («Ассемблерные вставки»): для них нужен `#use "asm"`, но JIT-функции пакета им не требуются.

Ошибки (неверный синтаксис, неизвестные мнемоника или регистр, неверная комбинация операндов, несовпадение размеров, число вне диапазона, неопределённая или повторная метка) возвращаются в `Result.Err` с номером строки. Вызов реализован через `unsafe.CallPtr` с маленькой ассемблерной прослойкой (`Call` собирает её этим же ассемблером), поэтому новые примитивы в компиляторе не нужны.

## c

Пакет для блоков `c { }` (см. `syntax.md`, «Вставки на C»): `#use "c"` обязателен в каждом файле, где есть такой блок. Это настоящий пакет из нескольких файлов, как `std/asm`: токенайзер C, набор инструментов для блоков, отображение типов и привязки к заголовкам libc, написанные на PGT. Доступные имена в режиме ядра указаны в конце раздела.

| Файл | Что внутри |
|---|---|
| `c.pgt` | `Version`, `Prelude` (три `#include`, которые компилятор кладёт в начало `.c` файла пакета), `BlockFuncName(pkg, n)`, `LineDirective(line, file)` |
| `errors.pgt` | `Result{Text, Err, Line, Col}`, `r.Ok()`, `r.Error()` |
| `types.pgt` | `Sizeof*` и `Alignof*` (`Char`, `Short`, `Int`, `Long`, `LongLong`, `Pointer`, `SizeT`, `Float`, `Double`, `Bool`), `CType(pgtType)`, `PgtType(cType)`, `SizeofType`, `AlignofType`, `IsScalarType`, `IsReadOnlyType` |
| `lexer.pgt` | `Tokenize(src) ([]Token, Result)` (без комментариев), `TokenizeAll` (с комментариями), `Token{Kind, Text, Line, Col, Value, Str}`, виды `TokIdent`, `TokKeyword`, `TokInt`, `TokFloat`, `TokChar`, `TokString`, `TokOp`, `TokPreproc`, `TokComment`, `KindName`, `IsKeyword` |
| `block.pgt` | `FindBlockEnd(src, start) int` (парная `}` с учётом строк, символов и комментариев, `-1` если не найдена), `Placeholders(text) []string`, `CheckBlock(text, names, types) Result`, `PrepareBlock(text, names, types, file, line, fname) Result`, `TopLevelBlock(text, file, line) string` |
| `pointers.pgt` | `CharPtr`, `GoString`, `CString`, `Null`, `IsNil`, `Add`, `Addr`, `LoadByte/Int/Int64/Ptr`, `StoreByte/Int/Int64/Ptr`, `Bytes` |
| `stdio.pgt` | `Puts`, `Putchar`, `Printf` и варианты с одним-двумя аргументами (`PrintfInt`, `PrintfInt64`, `PrintfStr`, `PrintfFloat`, `PrintfIntInt`, `PrintfStrInt`), `FprintfStr`, `FprintfInt`, `SprintfInt`, `SprintfInt64`, `SprintfStr`, `SprintfFloat` (возвращают `string`), `Fopen`, `Fclose`, `Fread`, `Fwrite`, `Fgets`, `Fputs`, `Fputc`, `Fgetc`, `Fflush`, `Feof`, `Remove`, `Rename`, `Stdin`, `Stdout`, `Stderr` |
| `stdlib.pgt` | `Malloc` (обнулённая память), `Calloc`, `Realloc`, `Free`, `Atoi`, `Atol`, `Atof`, `Strtol(s, base)`, `Strtod`, `Getenv`, `Setenv`, `Unsetenv`, `Exit`, `Abort`, `Rand`, `Srand`, `Abs`, `Labs`, `ZeroBytes`, `Dup` |
| `string.pgt` | `Strlen`, `Strcmp`, `Strncmp`, `Strcpy`, `Strncpy`, `Strcat`, `Strchr`, `Strrchr`, `Strstr`, `Strdup`, `Memcpy`, `Memmove`, `Memset`, `Memcmp` |
| `ctype.pgt` | `Isalpha`, `Isdigit`, `Isalnum`, `Isspace`, `Isupper`, `Islower`, `Ispunct`, `Isxdigit`, `Isprint`, `Toupper`, `Tolower` |
| `cmath.pgt` | `Sqrt`, `Pow`, `Sin`, `Cos`, `Tan`, `Floor`, `Ceil`, `Fabs`, `Fmod`, `Exp`, `Log`, `Log10`, `Log2`, `Atan`, `Atan2`, `Asin`, `Acos`, `Hypot`, `Cbrt` |
| `ctime.pgt` | `Time`, `Clock`, `Sleep`, `Usleep`, `Nanosleep(sec, nsec)`, `ClocksPerSec` |
| `errno.pgt` | `Errno`, `SetErrno`, `Strerror`, константы `EPERM`, `ENOENT`, `EINTR`, `EIO`, `EBADF`, `EAGAIN`, `ENOMEM`, `EACCES`, `EEXIST`, `EINVAL`, `ERANGE` и др. |
| `unistd.pgt` | `Getpid`, `Read`, `Write`, `Close`, `Lseek`, `Isatty`, `StdinFd`, `StdoutFd`, `StderrFd`, `SeekSet`, `SeekCur`, `SeekEnd` |
| `fcntl.pgt` | `Open(path, flags, mode)`, `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_APPEND`, `O_NONBLOCK`, `O_CLOEXEC` |
| `signal.pgt` | `Raise`, `Ignore`, `Default`, `SIGHUP`, `SIGINT`, `SIGQUIT`, `SIGILL`, `SIGABRT`, `SIGFPE`, `SIGKILL`, `SIGUSR1`, `SIGSEGV`, `SIGUSR2`, `SIGPIPE`, `SIGALRM`, `SIGTERM`, `SIGCHLD` |
| `shim.pgt` | Верхний блок `c { }` с обёртками для функций с переменным числом аргументов (`printf`, `open`, `nanosleep`, `signal`); сам использует `c { }` без импорта |

Типы блока: `int`, `int64`, `uint64`, `float`, `byte`, `char`, `bool`, `string` (только чтение), указатели `*T` (в `types` пишутся как `"int"`, `"string"`, `"*byte"`). Указатели libc — `*byte`; строки передаются как `string`.

`CheckBlock` не запускает C: он проверяет списки имён и типов, разбирает текст токенайзером (незакрытые строки, символы и комментарии, неверные цифры, пустой символ), проверяет баланс скобок и запрещает запись в переменные только для чтения (`{s} = ...`, `{s}++`). Ошибка содержит `Line` и `Col` внутри блока (первая строка — строка с `c {`). `PrepareBlock` после проверки собирает точно тот текст функции, что формирует компилятор:

```
void pgt_cblock_main_0(int32_t *v0, const char * const *v1)
{
#line 7 "dir/a.pgt"
...текст блока, {имя} заменено на (*vK)...
}
```

`names` — переменные, которые видны блоку (остальные `{x}` остаются обычными скобками C), `types` — их типы PGT.

В режиме ядра (`--freestanding`) загружаются только `c.pgt`, `types.pgt`, `errors.pgt`, `lexer.pgt`, `block.pgt`, `pointers.pgt` и `std/_freestanding/c/libc.pgt` (`Malloc`, `Calloc`, `Realloc`, `Free`, `Strlen`, `Strcmp`, `Memcpy`, `Memmove`, `Memset`, `Memcmp`, `Errno`, `SetErrno` поверх `pgt_kernel_rt.c`). Остальные привязки недоступны: `'c.Puts' needs libc and is not available in freestanding builds`.

## syscall

Системные вызовы, по каталогу на каждую ОС: `std/syscall/linux` (пакет `syscall_linux`) и `std/syscall/purec-os` (пакет `syscall_purec_os`). Имя пакета — `syscall_` плюс имя каталога, где `-` заменён на `_`.

`syscall_purec_os` — номера системных вызовов PureC OS из `src/kernel/syscall/syscall.h` ядра: 224 вызова и псевдонимы `SYS_OPEN`, `SYS_READ`, `SYS_CLOSE`, `SYS_UNLINK`, `SYS_RENAME`, `SYS_MKDIR`.

`syscall_linux` — номера системных вызовов Linux x86-64, как в Go. Все 386 констант взяты из `asm/unistd_64.h`: имя вызова в верхнем регистре с префиксом `SYS_`.

```pgt
#use "pgt/std/syscall/linux"

fmt.Println(syscall_linux.SYS_WRITE, " ", syscall_linux.SYS_EXIT)    // 1 60
```

Пока в пакете только номера; функции для прямых системных вызовов будут добавляться постепенно.

## net

TCP-сокеты (только IPv4), написаны на PGT поверх libc (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `read`); `sockaddr_in` собирается вручную через `unsafe`. Подключается как `#use "net"`. Ошибки возвращаются строкой, `""` означает успех.

- `Listen(host, port) (Listener, string)`: `host` — `localhost`, `0.0.0.0`, `""` или IPv4 вида `127.0.0.1`. Порт должен быть 1..65535. Порт ниже 1024 без root отклоняется до `bind`: `port N needs root: ports below 1024 are privileged, use 1024-65535`. Занятый порт: `port N is already in use`. Включён `SO_REUSEADDR`.
- `l.Accept() (Conn, string)`, `l.Close()`
- `Dial(host, port) (Conn, string)`
- `c.Read(max) (string, string)` (в конце потока данные `""` и ошибка `"eof"`), `c.ReadAll()`, `c.Write(s) string`, `c.WriteBytes(p, n)`, `c.Close()`, `c.RemoteAddr()`
- `ParseIP(host) ([]int, string)`, `SockAddr(ip, port) []byte`, `FormatAddr(sa) string`

Строки в PGT заканчиваются нулём, поэтому `Read` подходит для текста; бинарные данные нужно отправлять через `WriteBytes`.

## http

HTTP/1.1 в стиле Go `net/http`: `Connection: close`, один поток, поверх `net`. Подключается как `#use "http"`. Пример: `examples/httpdemo`. Исходники в `std/http`: `status.pgt` (все коды статусов Go с ссылками на RFC и `StatusText`), `method.pgt`, `header.pgt` (`Header`, `Values`, `URL`), `handler.pgt`, `request.pgt`, `response.pgt`, `mux.pgt`, `files.pgt`, `server.pgt`, `client.pgt`. Комментарии в `std/http` допускаются только со ссылкой на точный раздел RFC.

```pgt
#use (
    "http"
    "fmt"
)

fn hello(w http.ResponseWriter, r *http.Request) {
    fmt.Fprintf(w, "hello, %s", r.PathValue("name"))
}

fn main() {
    http.HandleFunc("GET /hello/{name}", hello)
    http.Handle("/static/", http.StripPrefix("/static/", http.FileServer(http.Dir("static"))))
    fmt.Println(http.ListenAndServe(":8080", nil))
}
return 0
```

Типы и интерфейсы:

- `interface Handler { ServeHTTP(w ResponseWriter, r *Request) }`; `HandlerFunc(f fn(ResponseWriter, *Request)) Handler` превращает функцию в `Handler` (у типов-функций нет методов, поэтому `HandlerFunc` здесь функция, а не тип).
- `interface ResponseWriter { Header() *Header; Write(s string) int; WriteBytes(b []byte) int; WriteHeader(code int) }`. Ответ буферизуется и отправляется после возврата из обработчика; `Content-Length` считается сам, `Content-Type` определяется по началу тела (`text/html` или `text/plain`), если не задан. `WriteHeader` после первой записи игнорируется.
- `*Header`: `NewHeader()`, `Set`, `Add`, `Get`, `Values`, `Has`, `Del`, `Keys`; ключи приводятся к виду `Content-Type` (`CanonicalHeaderKey`, RFC 9110, 5.1).
- `Values` (запрос и форма): `NewValues()`, `Set`, `Add`, `Get`, `All`, `Has`, `Del`, `Encode()` (ключи по алфавиту), `ParseQuery(raw)`, `QueryEscape`, `QueryUnescape`, `PathEscape`, `PathUnescape`.
- `*URL{Scheme, Host, Path, RawQuery}`: `Query() Values`, `String()`. `Path` декодирован.
- `*Request{Method, URL, Proto, Header, Body, Host, RemoteAddr, ContentLength, RequestURI}`: `FormValue(key)` (тело `application/x-www-form-urlencoded` и строка запроса), `ParseForm()`, `PathValue(name)`, `SetPathValue`, `Cookie(name) string` (значение или `""`), `UserAgent()`, `Referer()`. Тело в виде строки; `Transfer-Encoding` у запроса даёт 501, тело больше 8 МБ даёт 413.

Константы: все коды статусов Go `StatusContinue` (100) ... `StatusNetworkAuthenticationRequired` (511) и `StatusText(code) string`; методы `MethodGet`, `MethodHead`, `MethodPost`, `MethodPut`, `MethodPatch`, `MethodDelete`, `MethodConnect`, `MethodOptions`, `MethodTrace`.

Маршрутизация:

- `NewServeMux()`, `mux.Handle(pattern, h)`, `mux.HandleFunc(pattern, f)`, `mux.ServeHTTP`. Глобальные `DefaultServeMux`, `http.Handle`, `http.HandleFunc`.
- Шаблоны Go 1.22: `"GET /api/items"` (метод необязателен; `GET` покрывает и `HEAD`), `"/static/"` (поддерево), `"/users/{id}"`, `"/files/{path...}"` (остаток пути), `"/{$}"` (только `/`). Побеждает самый специфичный шаблон: литерал сильнее `{id}`, `{id}` сильнее `{rest...}`, точный путь сильнее поддерева, шаблон с методом сильнее шаблона без метода. Нет подходящего пути: 404; путь есть, метода нет: 405 с `Allow`. Повторная регистрация шаблона и неверный шаблон вызывают `panic`. Путь не очищается от `..` и не редиректится, шаблоны с хостом не поддерживаются.

Сервер:

- `ListenAndServe(addr, h) string`: `addr` вида `":8080"` или `"127.0.0.1:8080"`; `nil` вместо `h` — `DefaultServeMux`; возвращает текст ошибки, пока работает, блокирует. `Server{Addr, Handler}` с `srv.ListenAndServe()` и `srv.Close()` (можно вызвать из обработчика). Лог выключен. Порт ниже 1024 без root: `port N needs root: ports below 1024 are privileged, use 1024-65535`, занятый: `port N is already in use`.
- `Error(w, msg, code)`, `NotFound(w, r)`, `NotFoundHandler()`, `Redirect(w, r, url, code)`, `RedirectHandler(url, code)`, `StripPrefix(prefix, h)`.
- `ServeFile(w, r, path)` (для папки отдаётся `index.html`; файл отдаётся потоком, бинарные безопасны), `FileServer(http.Dir(root))`: `index.html`, `..` или `\` в пути дают 403, нет файла 404. `Mime(path)`.

fmt: `fmt.Fprint`, `fmt.Fprintln`, `fmt.Fprintf` принимают первым аргументом `any` с методом `Write(s string) int` (интерфейс `fmt.Writer`); интерфейс нельзя передать в параметр другого интерфейса, поэтому параметр имеет тип `any`, а проверка выполняется утверждением типа; без метода — `panic`.

Клиент (только `http://`, для `https://` ошибка `https is not supported yet`):

- `Get(url)`, `Head(url)`, `Delete(url)`, `Post(url, contentType, body)`, `Put(url, contentType, body)` возвращают `(*Response, string)`; `Response{StatusCode, Status, Header, Body}`, `Status` вида `200 OK`. `Transfer-Encoding: chunked` разбирается.
