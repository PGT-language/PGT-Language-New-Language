#include "scaffold/Templates.h"

namespace pgt {

const std::vector<ProjectTemplate>& projectTemplates() {
    static const std::vector<ProjectTemplate> templates = {
        {"hello",
         "prints Hello, World!",
         {{"main.pgt", R"PGT(package main

fn main() {
    println("Hello, World!")
}

return 0
)PGT"}}},

        {"calc",
         "reads two numbers and prints the results of the arithmetic operations",
         {{"main.pgt", R"PGT(package main

fn main() {
    var* a = 0
    var* b = 0
    input("First number: ", a)
    input("Second number: ", b)

    println(a, " + ", b, " = ", a + b)
    println(a, " - ", b, " = ", a - b)
    println(a, " * ", b, " = ", a * b)

    if b == 0 {
        println("Division by zero is not allowed")
    } else {
        println(a, " / ", b, " = ", a / b)
        println(a, " % ", b, " = ", a % b)
    }
}

return 0
)PGT"}}},

        {"fizzbuzz",
         "classic FizzBuzz: loops, conditions and the remainder operator",
         {{"main.pgt", R"PGT(package main

fn main() {
    for i in 1..101 {
        if i % 15 == 0 {
            println("FizzBuzz")
        } else if i % 3 == 0 {
            println("Fizz")
        } else if i % 5 == 0 {
            println("Buzz")
        } else {
            println(i)
        }
    }
}

return 0
)PGT"},
          }},

        {"stdlib",
         "word statistics with structs, arrays and the standard library",
         {{"main.pgt", R"PGT(package main

#use (
    "strings"
    "arrays"
    "io"
)

struct Stats {
    Lines int
    Words []string
}

fn (s *Stats) Add(line string) {
    s.Lines++
    for w in strings.Fields(strings.Lower(line)) {
        s.Words = append(s.Words, strings.Trim(w, ".,!?;:"))
    }
}

fn main() {
    println("Type some text, finish with Ctrl+D:")
    var* stats Stats
    for line in io.ReadLines() {
        stats.Add(line)
    }
    arrays.SortStrings(stats.Words)
    println("lines: ", stats.Lines, ", words: ", len(stats.Words))
    println("sorted: ", strings.Join(stats.Words, " "))
}

return 0
)PGT"}}},

        {"packages",
         "a module with its own package imported through #use",
         {{"main.pgt", R"PGT(package main

#use "{{module}}/greet"

fn main() {
    greet.Hello()
}

return 0
)PGT"},
          {"greet/greet.pgt", R"PGT(package greet

fn Hello() {
    println("Hello from the greet package!")
}
)PGT"}}},
    };
    return templates;
}

const ProjectTemplate* findTemplate(const std::string& name) {
    for (const ProjectTemplate& t : projectTemplates()) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

}
