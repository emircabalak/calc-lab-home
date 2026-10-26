// Emir Cabalak 260AEB076
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: https://en.cppreference.com/c/numeric/math (pow, isinf, isnan)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define MY_NAME "emir"
#define MY_LASTNAME "cabalak"
#define MY_ID "260AEB076"

#define PATH_SIZE 4096

// A number can be an integer or a float. If is_float is 0 the value is in i,
// otherwise it is in f. A number is a float only when it is written with a
// dot, like 2.5. calculate() uses long long when both numbers are integers.
// If at least one of them is a float, it converts both to double, so 7 / 2
// gives 3 but 7 / 2.0 gives 3.5.
typedef struct {
    int is_float;
    long long i;
    double f;
} Value;

// The whole input file is loaded into text. The tokenizer reads from it
// directly with the index pos, so the program never copies single lines out
// of the file.
// For a file of several megabytes this matters, because copying every line
// again would make the program a lot slower.
char *text;
long text_len;
long pos;

// The current token. tok is one character that says what kind of token it is:
//   'n' number, '+', '-', '*', '/', '^' for **, '(', ')',
//   'E' end of the line, '?' invalid character
char tok;
long tok_pos;
Value tok_value;

// 0 means no error yet. Otherwise it is the position of the first error.
long error_pos;

void set_error(long where) {
    // Only the first error is reported, so later errors are ignored.
    if (error_pos == 0) {
        error_pos = where;
    }
}

int is_digit(char c) {
    return c >= '0' && c <= '9';
}

// How positions work: pos is an index into the whole file, starting at 0.
// The rules count from 1, so a token that starts at index pos has the
// position pos + 1. Because pos goes through every character of the file,
// the newlines, the spaces and the comment lines before a token are already
// counted, and there is no need for a separate line or column number.
void read_number(void) {
    long start = pos;
    while (pos < text_len && is_digit(text[pos])) {
        pos++;
    }

    if (pos < text_len && text[pos] == '.') {
        // A float needs digits after the dot. For "5." or "5.+1" there are
        // none, so the error is at the dot itself.
        if (pos + 1 >= text_len || !is_digit(text[pos + 1])) {
            tok = '?';
            tok_pos = pos + 1;
            return;
        }

        // The float is built from the digits by hand. For "12.25" the loop
        // makes 1225 and then it is divided by 100. strtod would also read things like
        // "1.5e3", but here the "e" must be an error.
        double number = 0;
        double divide_by = 1;
        int after_dot = 0;
        for (long k = start; k < text_len; k++) {
            if (text[k] == '.' && !after_dot) {
                after_dot = 1;
            }
            else if (is_digit(text[k])) {
                number = number * 10 + (text[k] - '0');
                if (after_dot) {
                    divide_by = divide_by * 10;
                }
            }
            else {
                break;
            }
            pos = k + 1;
        }
        tok = 'n';
        tok_value.is_float = 1;
        tok_value.f = number / divide_by;
        tok_value.i = 0;
        return;
    }

    long long number = 0;
    for (long k = start; k < pos; k++) {
        number = number * 10 + (text[k] - '0');
    }
    tok = 'n';
    tok_value.is_float = 0;
    tok_value.i = number;
    tok_value.f = 0;
}

void next_token(void) {
    while (pos < text_len && (text[pos] == ' ' || text[pos] == '\t')) {
        pos++;
    }
    tok_pos = pos + 1;

    // The '\n' is not skipped here. Its position is where a missing number or a
    // missing ')' at the end of a line is reported. If the last line has no
    // '\n', pos is equal to text_len here, so the position becomes the file
    // length + 1, like the rules say.
    if (pos >= text_len || text[pos] == '\n') {
        tok = 'E';
        return;
    }

    char c = text[pos];
    if (is_digit(c)) {
        read_number();
        return;
    }

    pos++;
    if (c == '*' && pos < text_len && text[pos] == '*') {
        pos++;
        tok = '^';
    }
    else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')') {
        tok = c;
    }
    else {
        // Every other character is invalid, also a '#' in the middle of a
        // line. The parser stops when it sees this token, so the error is at
        // this character.
        tok = '?';
    }
}

long long int_power(long long base, long long exp) {
    if (base == 0) {
        if (exp == 0) {
            return 1;
        }
        return 0;
    }
    if (base == 1) {
        return 1;
    }
    if (base == -1) {
        if (exp % 2 == 0) {
            return 1;
        }
        return -1;
    }
    // For any other base the result gets big fast, and the tests never
    // overflow, so this loop runs at most about 63 times.
    long long result = 1;
    for (long long k = 0; k < exp; k++) {
        result = result * base;
    }
    return result;
}

// Does one calculation. op_pos is where the operator is, right_pos is where
// the right number (or the right part in brackets) starts.
// Why the errors are reported where they are:
//   - division by zero: at right_pos, because the right side is the problem,
//     and it can be a whole bracket like (4 - 2 * 2)
//   - negative integer exponent: also at right_pos, for the same reason
//   - float result is inf or nan: at op_pos, because both numbers are fine on
//     their own and only the operation goes wrong
Value calculate(char op, Value a, Value b, long op_pos, long right_pos) {
    Value result;
    result.is_float = 0;
    result.i = 0;
    result.f = 0;

    if (op == '/') {
        if ((b.is_float && b.f == 0.0) || (!b.is_float && b.i == 0)) {
            set_error(right_pos);
            return result;
        }
    }

    if (a.is_float || b.is_float) {
        double x = a.f;
        double y = b.f;
        if (!a.is_float) {
            x = (double)a.i;
        }
        if (!b.is_float) {
            y = (double)b.i;
        }

        double r;
        if (op == '+') {
            r = x + y;
        }
        else if (op == '-') {
            r = x - y;
        }
        else if (op == '*') {
            r = x * y;
        }
        else if (op == '/') {
            r = x / y;
        }
        else {
            r = pow(x, y);
        }

        if (isinf(r) || isnan(r)) {
            set_error(op_pos);
            return result;
        }
        result.is_float = 1;
        result.f = r;
        return result;
    }

    if (op == '+') {
        result.i = a.i + b.i;
    }
    else if (op == '-') {
        result.i = a.i - b.i;
    }
    else if (op == '*') {
        result.i = a.i * b.i;
    }
    else if (op == '/') {
        // In C integer division already cuts toward zero, so (0 - 7) / 2
        // is -3 like the rules want.
        result.i = a.i / b.i;
    }
    else {
        if (b.i < 0) {
            set_error(right_pos);
            return result;
        }
        result.i = int_power(a.i, b.i);
    }
    return result;
}

// The parser. There is one function for each level of precedence:
//
//   expr    := term  { ('+' | '-') term }
//   term    := power { ('*' | '/') power }
//   power   := primary [ '**' power ]
//   primary := NUMBER | '(' expr ')'
//
// parse_expr() gets its numbers from parse_term(), and parse_term() gets them
// from parse_power(). So when parse_expr() sees a '+', all the '*' and '/' on
// both sides are already calculated. That is why 3 + 2 * 5 is 13 and not 25.
// A bracket starts again at parse_expr(), so everything inside it is
// calculated before the operator outside the bracket uses it.
//
// The result is calculated while parsing. So the first error the parser meets
// when going from left to right is the one that gets reported.
//
// When the parser can't continue, the error is at tok_pos, the start of the
// token it is looking at. For "7 + * 3" that is the second operator, for
// "3 *" at the end of a line it is the '\n', and for "(1 + 2))" it is the
// extra ')'.

Value parse_expr(void);

Value parse_primary(void) {
    Value v;
    v.is_float = 0;
    v.i = 0;
    v.f = 0;

    if (tok == 'n') {
        v = tok_value;
        next_token();
        return v;
    }

    if (tok == '(') {
        next_token();
        v = parse_expr();
        if (error_pos != 0) {
            return v;
        }
        // If the ')' is missing at the end of the line, tok is 'E' here, so
        // the error is at the '\n'.
        if (tok != ')') {
            set_error(tok_pos);
            return v;
        }
        next_token();
        return v;
    }

    // There is no unary minus, so "-5" ends up here with the error at '-'.
    set_error(tok_pos);
    return v;
}

// One number in a chain like a ** b ** c, with the positions needed for errors.
typedef struct {
    Value value;
    long start;   // where this number (or bracket) starts
    long op_pos;  // where the ** after it is
} PowerPart;

// ** is right associative: 2 ** 3 ** 2 is 2 ** (3 ** 2) = 512 and not
// (2 ** 3) ** 2 = 64. The grammar rule "power := primary [ '**' power ]" says
// this, because the exponent is again a whole power.
//
// Written with recursion like the rule, every ** would need one more function
// call on the stack, and a very long line like "1 ** 1 ** 1 ** ..." in a big
// test file could crash the program. So this function reads the whole chain
// into an array first and then calculates it from the right end to the left.
// That gives the same result as the recursive version.
Value parse_power(void) {
    long first_start = tok_pos;
    Value first = parse_primary();
    if (error_pos != 0 || tok != '^') {
        return first;
    }

    int size = 16;
    int count = 1;
    PowerPart *parts = malloc(size * sizeof(PowerPart));
    if (parts == NULL) {
        fprintf(stderr, "calc: not enough memory\n");
        exit(1);
    }
    parts[0].value = first;
    parts[0].start = first_start;

    while (tok == '^') {
        parts[count - 1].op_pos = tok_pos;
        next_token();

        if (count == size) {
            size = size * 2;
            PowerPart *bigger = realloc(parts, size * sizeof(PowerPart));
            if (bigger == NULL) {
                fprintf(stderr, "calc: not enough memory\n");
                free(parts);
                exit(1);
            }
            parts = bigger;
        }

        parts[count].start = tok_pos;
        parts[count].value = parse_primary();
        count++;
        if (error_pos != 0) {
            free(parts);
            return first;
        }
    }

    // Calculate from the right. The exponent of the k-th ** is everything to
    // its right, and that part starts where number k + 1 starts.
    Value result = parts[count - 1].value;
    for (int k = count - 2; k >= 0; k--) {
        result = calculate('^', parts[k].value, result, parts[k].op_pos, parts[k + 1].start);
        if (error_pos != 0) {
            break;
        }
    }
    free(parts);
    return result;
}

// The loop calculates each '*' or '/' right away, so 100 / 7 / 2 is
// (100 / 7) / 2. That makes them left associative.
Value parse_term(void) {
    Value left = parse_power();
    while (error_pos == 0 && (tok == '*' || tok == '/')) {
        char op = tok;
        long op_pos = tok_pos;
        next_token();
        long right_pos = tok_pos;
        Value right = parse_power();
        if (error_pos != 0) {
            return left;
        }
        left = calculate(op, left, right, op_pos, right_pos);
    }
    return left;
}

Value parse_expr(void) {
    Value left = parse_term();
    while (error_pos == 0 && (tok == '+' || tok == '-')) {
        char op = tok;
        long op_pos = tok_pos;
        next_token();
        long right_pos = tok_pos;
        Value right = parse_term();
        if (error_pos != 0) {
            return left;
        }
        left = calculate(op, left, right, op_pos, right_pos);
    }
    return left;
}

// Copies the last part of a path into out. "labs/task1.txt" becomes "task1"
// when remove_txt is 1, and "../labs/" becomes "labs".
void get_base_name(const char *path, char *out, int remove_txt) {
    char copy[PATH_SIZE];
    snprintf(copy, sizeof(copy), "%s", path);

    int n = (int)strlen(copy);
    while (n > 1 && copy[n - 1] == '/') {
        copy[n - 1] = '\0';
        n--;
    }

    char *name = strrchr(copy, '/');
    if (name == NULL) {
        name = copy;
    }
    else {
        name++;
    }

    n = (int)strlen(name);
    if (remove_txt && n >= 4 && strcmp(name + n - 4, ".txt") == 0) {
        name[n - 4] = '\0';
    }
    snprintf(out, PATH_SIZE, "%s", name);
}

int ends_with_txt(const char *name) {
    int n = (int)strlen(name);
    return n >= 4 && strcmp(name + n - 4, ".txt") == 0;
}

// Reads the whole file with one fread. Returns 0 if it worked.
int load_file(const char *file_name) {
    FILE *f = fopen(file_name, "rb");
    if (f == NULL) {
        fprintf(stderr, "calc: cannot open %s: %s\n", file_name, strerror(errno));
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fprintf(stderr, "calc: cannot read %s\n", file_name);
        fclose(f);
        return 1;
    }

    text = malloc(size + 1);
    if (text == NULL) {
        fprintf(stderr, "calc: not enough memory for %s\n", file_name);
        fclose(f);
        return 1;
    }
    text_len = (long)fread(text, 1, size, f);
    fclose(f);

    if (text_len != size) {
        fprintf(stderr, "calc: cannot read %s\n", file_name);
        free(text);
        text = NULL;
        return 1;
    }
    return 0;
}

// Calculates one input file and writes the output file. Returns 0 if it worked.
int calc_file(const char *file_name, const char *out_dir) {
    if (load_file(file_name) != 0) {
        return 1;
    }

    char base[PATH_SIZE];
    char out_name[PATH_SIZE + 600];
    get_base_name(file_name, base, 1);
    snprintf(out_name, sizeof(out_name), "%s/%s_%s_%s_%s.txt",
             out_dir, base, MY_NAME, MY_LASTNAME, MY_ID);

    // "wb" so the file gets plain '\n' line endings on every system.
    FILE *out = fopen(out_name, "wb");
    if (out == NULL) {
        fprintf(stderr, "calc: cannot write %s: %s\n", out_name, strerror(errno));
        free(text);
        text = NULL;
        return 1;
    }

    pos = 0;
    error_pos = 0;
    while (pos < text_len) {
        // skip spaces and tabs at the start of the line
        while (pos < text_len && (text[pos] == ' ' || text[pos] == '\t')) {
            pos++;
        }
        if (pos >= text_len) {
            break;
        }

        // blank line
        if (text[pos] == '\n') {
            pos++;
            continue;
        }

        // comment line: jump to the next line
        if (text[pos] == '#') {
            while (pos < text_len && text[pos] != '\n') {
                pos++;
            }
            pos++;
            continue;
        }

        next_token();
        Value v = parse_expr();

        // After a full expression only the end of the line may come. Anything
        // else, like an extra ')' or a second number, is an error there.
        if (error_pos == 0 && tok != 'E') {
            set_error(tok_pos);
        }
        if (error_pos != 0) {
            break;
        }

        if (v.is_float) {
            fprintf(out, "%.15g\n", v.f);
        }
        else {
            fprintf(out, "%lld\n", v.i);
        }

        // pos is at the '\n' that ended this line, so skip it
        pos++;
    }

    // If there was an error, the results written so far must go away. Opening
    // the file again with "wb" makes it empty, and then only the error is
    // written. This way the results go straight into the file while reading,
    // and big files don't need all their results kept in memory.
    if (error_pos != 0) {
        fclose(out);
        out = fopen(out_name, "wb");
        if (out == NULL) {
            fprintf(stderr, "calc: cannot write %s: %s\n", out_name, strerror(errno));
            free(text);
            text = NULL;
            return 1;
        }
        fprintf(out, "ERROR:%ld\n", error_pos);
    }

    int result = 0;
    if (fclose(out) != 0) {
        fprintf(stderr, "calc: cannot write %s\n", out_name);
        result = 1;
    }
    free(text);
    text = NULL;
    return result;
}

// Makes the output folder if it does not exist yet. Returns 0 if it worked.
int make_folder(const char *path) {
    struct stat info;
    if (stat(path, &info) == 0) {
        if (S_ISDIR(info.st_mode)) {
            return 0;
        }
        fprintf(stderr, "calc: %s is not a folder\n", path);
        return 1;
    }
    if (mkdir(path, 0777) != 0) {
        fprintf(stderr, "calc: cannot create folder %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}

void print_usage(void) {
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

int main(int argc, char *argv[]) {
    char *dir = NULL;
    char *out_dir = NULL;
    char *input = NULL;

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-d") == 0 || strcmp(argv[k], "--dir") == 0) {
            if (k + 1 >= argc) {
                fprintf(stderr, "calc: %s needs a folder name\n", argv[k]);
                return 1;
            }
            dir = argv[k + 1];
            k++;
        }
        else if (strcmp(argv[k], "-o") == 0 || strcmp(argv[k], "--output-dir") == 0) {
            if (k + 1 >= argc) {
                fprintf(stderr, "calc: %s needs a folder name\n", argv[k]);
                return 1;
            }
            out_dir = argv[k + 1];
            k++;
        }
        else if (argv[k][0] == '-') {
            fprintf(stderr, "calc: unknown option %s\n", argv[k]);
            print_usage();
            return 1;
        }
        else if (input != NULL) {
            fprintf(stderr, "calc: only one input file is allowed\n");
            print_usage();
            return 1;
        }
        else {
            input = argv[k];
        }
    }

    if (dir == NULL && input == NULL) {
        fprintf(stderr, "calc: no input file or folder given\n");
        print_usage();
        return 1;
    }
    if (dir != NULL && input != NULL) {
        fprintf(stderr, "calc: use -d or an input file, not both\n");
        print_usage();
        return 1;
    }

    // The input is checked before the output folder is made, so a wrong input
    // name does not leave an empty folder behind.
    DIR *folder = NULL;
    if (dir != NULL) {
        folder = opendir(dir);
        if (folder == NULL) {
            fprintf(stderr, "calc: cannot open folder %s: %s\n", dir, strerror(errno));
            return 1;
        }
    }
    else {
        struct stat info;
        if (stat(input, &info) != 0) {
            fprintf(stderr, "calc: cannot open %s: %s\n", input, strerror(errno));
            return 1;
        }
        if (!S_ISREG(info.st_mode)) {
            fprintf(stderr, "calc: %s is not a file\n", input);
            return 1;
        }
    }

    // Without -o the output folder is <input_base>_<name>_<id>.
    char default_dir[PATH_SIZE + 100];
    if (out_dir == NULL) {
        char base[PATH_SIZE];
        if (dir != NULL) {
            get_base_name(dir, base, 0);
        }
        else {
            get_base_name(input, base, 1);
        }
        snprintf(default_dir, sizeof(default_dir), "%s_%s_%s", base, MY_NAME, MY_ID);
        out_dir = default_dir;
    }

    if (make_folder(out_dir) != 0) {
        if (folder != NULL) {
            closedir(folder);
        }
        return 1;
    }

    int failed = 0;
    if (folder == NULL) {
        failed = calc_file(input, out_dir);
    }
    else {
        // Only *.txt files directly in the folder. Subfolders and other files
        // are skipped.
        struct dirent *entry;
        while ((entry = readdir(folder)) != NULL) {
            if (!ends_with_txt(entry->d_name)) {
                continue;
            }
            char path[PATH_SIZE + 300];
            snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

            struct stat info;
            if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
                if (calc_file(path, out_dir) != 0) {
                    failed = 1;
                }
            }
        }
        closedir(folder);
    }

    return failed;
}
