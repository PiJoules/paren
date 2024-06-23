#ifndef LIBPAREN_H
#define LIBPAREN_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define PAREN_VERSION "1.9.8"

inline constexpr std::string_view kParenInitName = "paren_init";
inline constexpr std::string_view kParenEvalStringName = "paren_eval_string";
inline constexpr std::string_view kParenImportName = "paren_import";

extern "C" {

// FFI
void paren_eval_string(const char *);
void paren_import(const char *);
void paren_init();

}  // extern "C"

namespace libparen {

struct Node;
struct environment;
struct paren;
typedef std::shared_ptr<Node> snode;
typedef std::shared_ptr<std::thread> sthread;
typedef std::shared_ptr<environment> senvironment;
typedef std::thread *pthread;
typedef snode (*builtin)(std::vector<snode> &args, senvironment &env);

struct Node {
  enum {
    T_NIL,
    T_INT,
    T_DOUBLE,
    T_BOOL,
    T_STRING,
    T_SYMBOL,
    T_LIST,
    T_SPECIAL,
    T_BUILTIN,
    T_FN,
    T_THREAD
  } type;
  union {
    int v_int;
    size_t code;  // if T_SYMBOL, code for faster access
    double v_double;
    bool v_bool;
    pthread p_thread = nullptr;
    builtin v_builtin;
  };
  std::string v_string;
  std::vector<snode> v_list;
  senvironment outer_env;  // if T_FN
                           // sthread s_thread;

  Node();
  Node(int a);
  Node(double a);
  Node(bool a);
  Node(const std::string &a);
  Node(const std::vector<snode> &a);
  Node(builtin a);

  int to_int();             // convert to int
  double to_double();       // convert to double
  std::string to_string();  // convert to std::string
  std::string type_str();
  std::string str_with_type();
};

struct environment {
  std::map<size_t, snode> env;
  senvironment outer;
  environment();
  environment(senvironment outer);
  snode get(size_t code);
  snode get(snode &k);
  snode set(snode &k, snode &v);
};

void init();

snode eval(snode &n, senvironment &env);
snode eval_all(std::vector<snode> &lst);
snode compile(snode &n);
std::vector<snode> compile_all(std::vector<snode> &lst);
snode apply(snode &func, std::vector<snode> &args, senvironment &env);
void print_logo();
void prompt();
void prompt2();
snode eval_string(std::string &s);
snode eval_string(const char *s);
inline void eval_print(std::string &s);
void repl();  // read-eval-print loop

snode get(const char *name);
void set(const char *name, Node value);

inline double rand_double();
bool slurp(std::string_view filename, std::string &str);
int spit(std::string_view filename, std::string_view str);
size_t ToCode(std::string_view name);
snode make_snode(const Node &n);

std::vector<std::string> tokenize(const std::string &s);
std::vector<snode> parse(const std::string &s);
}  // namespace libparen
#endif
