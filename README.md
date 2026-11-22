# FIRST, FOLLOW & LL(1) Parser in C

A console-based Compiler Design project developed in C to calculate FIRST and FOLLOW sets, construct an LL(1) parsing table, and check whether an input string is accepted or rejected by a given grammar.

## Project Information

- **Project Name:** FIRST, FOLLOW & LL(1) Parser
- **Language:** C
- **Project Type:** Console-Based Application
- **Course:** Compiler Design
- **Developed By:** Tarun Ghosh


## Features

- Accepts user-defined grammar productions.
- Calculates FIRST sets.
- Calculates FOLLOW sets.
- Handles epsilon productions.
- Constructs an LL(1) parsing table.
- Detects LL(1) parsing table conflicts.
- Parses input strings using the LL(1) parsing method.
- Displays parsing steps and stack trace.
- Shows whether the input string is ACCEPTED or REJECTED.

## Grammar Example

```text
E -> T E'
E' -> + T E' | eps
T -> F T'
T' -> * F T' | eps
F -> ( E ) | id
```

## Sample Accepted Strings

```text
id
id+id
id+id*id
(id)
(id+id)*id
id*(id+id)
```

## Sample Rejected Strings

```text
+id
id+
(id
id)
id++id
```

## How to Run

### 1. Compile

```bash
gcc -std=c11 FirstFollow_LL1_fixed.c -o parser
```

### 2. Run

```bash
FirstFollow_LL1_fixed.exe
```

### 3. Enter Grammar

Type:

```text
default
```

to load the default arithmetic expression grammar.

You can also enter your own grammar productions.


## Project Purpose

This project demonstrates fundamental concepts of Compiler Design, including grammar analysis, FIRST and FOLLOW set computation, LL(1) parsing table construction, and syntax checking using a predictive parser.

## Author -- Tarun Ghosh

**Tarun Ghosh**  
**ID: 20244103154**
