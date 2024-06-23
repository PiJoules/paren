#include "libparen.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace libparen {

senvironment global_env;  // variables

Node::Node() : type(T_NIL) {}
Node::Node(int a) : type(T_INT), v_int(a) {}
Node::Node(double a) : type(T_DOUBLE), v_double(a) {}
Node::Node(bool a) : type(T_BOOL), v_bool(a) {}
Node::Node(const std::string &a) : type(T_STRING), v_string(a) {}
Node::Node(const std::vector<snode> &a) : type(T_LIST), v_list(a) {}
Node::Node(builtin a) : type(T_BUILTIN), v_builtin(a) {}
snode make_special(builtin a) {
  Node n;
  n.type = Node::T_SPECIAL;
  n.v_builtin = a;
  return std::make_shared<Node>(n);
}

snode node_true(std::make_shared<Node>(Node(true)));
snode node_false(std::make_shared<Node>(Node(false)));
snode node_0(std::make_shared<Node>(Node(0)));
snode node_1(std::make_shared<Node>(Node(1)));
snode nil(std::make_shared<Node>(Node()));

snode builtin_prn(std::vector<snode> &args, senvironment &env);

inline int Node::to_int() {
  switch (type) {
    case T_INT:
      return v_int;
    case T_DOUBLE:
      return (int)v_double;
    case T_BOOL:
      return (int)v_bool;
    case T_STRING:
      return atoi(v_string.c_str());
    default:
      return 0;
  }
}

inline double Node::to_double() {
  switch (type) {
    case T_INT:
      return v_int;
    case T_DOUBLE:
      return v_double;
    case T_BOOL:
      return v_bool;
    case T_STRING:
      return atof(v_string.c_str());
    default:
      return 0.0;
  }
}
inline std::string Node::to_string() {
  char buf[32];
  std::string ret;
  switch (type) {
    case T_NIL:
      break;
    case T_INT:
      sprintf(buf, "%d", v_int);
      ret = buf;
      break;
    case T_BUILTIN:
      sprintf(buf, "#<builtin:%p>", v_builtin);
      ret = buf;
      break;
    case T_SPECIAL:
      sprintf(buf, "#<builtin:%p>", v_builtin);
      ret = buf;
      break;
    case T_DOUBLE:
      sprintf(buf, "%.16g", v_double);
      ret = buf;
      break;
    case T_BOOL:
      return (v_bool ? "true" : "false");
    case T_STRING:
    case T_SYMBOL:
      return v_string;
    case T_FN:
    case T_LIST: {
      ret = '(';
      for (std::vector<snode>::iterator iter = v_list.begin();
           iter != v_list.end(); iter++) {
        if (iter != v_list.begin())
          ret += ' ';
        ret += (*iter)->to_string();
      }
      ret += ')';
      break;
    }
    default:;
  }
  return ret;
}
inline std::string Node::type_str() {
  switch (type) {
    case T_NIL:
      return "nil";
    case T_INT:
      return "int";
    case T_DOUBLE:
      return "double";
    case T_BOOL:
      return "bool";
    case T_STRING:
      return "std::string";
    case T_SYMBOL:
      return "symbol";
    case T_LIST:
      return "list";
    case T_BUILTIN:
      return "builtin";
    case T_SPECIAL:
      return "special";
    case T_FN:
      return "fn";
    case T_THREAD:
      return "std::thread";
    default:
      return "invalid type";
  }
}
inline std::string Node::str_with_type() {
  return to_string() + " : " + type_str();
}

inline double rand_double() {
  return (double)rand() / ((double)RAND_MAX + 1.0);
}

class Tokenizer {
 private:
  std::vector<std::string> ret;
  std::string acc;  // accumulator
  std::string s;
  void emit() {  // add accumulated std::string to token list
    if (acc.length() > 0) {
      ret.push_back(acc);
      acc = "";
    }
  }

 public:
  int unclosed;  // number of unclosed parenthesis ( or quotation "
  Tokenizer(const std::string &s) : s(s), unclosed(0) {}

  std::vector<std::string> tokenize() {
    size_t last = s.length() - 1;
    for (size_t pos = 0; pos <= last; pos++) {
      char c = s.at(pos);
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        emit();
      } else if (c == ';' ||
                 (pos < last && c == '#' &&
                  s[pos + 1] == '!')) {  // end-of-line comment: ; or #!
        emit();
        do
          pos++;
        while (pos <= last && s.at(pos) != '\n');
      } else if (c == '"') {  // beginning of std::string
        unclosed++;
        emit();
        acc += '"';
        pos++;
        while (pos <= last) {
          if (s.at(pos) == '"') {
            unclosed--;
            break;
          }
          if (s.at(pos) == '\\') {  // escape
            char next = s.at(pos + 1);
            if (next == 'r')
              next = '\r';
            else if (next == 'n')
              next = '\n';
            else if (next == 't')
              next = '\t';
            acc += next;
            pos += 2;
          } else {
            acc += s.at(pos);
            pos++;
          }
        }
        emit();
      } else if (c == '(') {
        unclosed++;
        emit();
        acc += c;
        emit();
      } else if (c == ')') {
        unclosed--;
        emit();
        acc += c;
        emit();
      } else {
        acc += c;
      }
    }
    emit();
    return ret;
  }
};

std::vector<std::string> tokenize(const std::string &s) {
  return Tokenizer(s).tokenize();
}

std::map<std::string, size_t, std::less<>> symcode;
std::vector<std::string> symname;

size_t ToCode(std::string_view name) {
  auto found = symcode.find(name);
  if (found != symcode.end()) {
    return found->second;
  } else {
    size_t r = symcode.size();
    symcode[std::string(name)] = r;
    symname.emplace_back(name);
    return r;
  }
}

class Parser {
 private:
  size_t pos;
  std::vector<std::string> tokens;

 public:
  Parser(const std::vector<std::string> &tokens) : pos(0), tokens(tokens) {}
  std::vector<snode> parse() {
    std::vector<snode> ret;
    for (; pos < tokens.size(); pos++) {
      std::string tok = tokens.at(pos);
      if (tok[0] < 0)
        break;                 // MSVC bug fix
      if (tok.at(0) == '"') {  // double-quoted std::string
        ret.push_back(std::make_shared<Node>(tok.substr(1)));
      } else if (tok == "(") {  // list
        pos++;
        ret.push_back(std::make_shared<Node>(parse()));
      } else if (tok == ")") {  // end of list
        break;
      } else if (isdigit(tok.at(0)) || (tok.at(0) == '-' && tok.length() >= 2 &&
                                        isdigit(tok.at(1)))) {  // number
        if (tok.find('.') != std::string::npos ||
            tok.find('e') != std::string::npos) {  // double
          ret.push_back(std::make_shared<Node>(atof(tok.c_str())));
        } else {
          ret.push_back(std::make_shared<Node>(atoi(tok.c_str())));
        }
      } else {  // symbol
        Node n;
        n.type = Node::T_SYMBOL;
        n.v_string = tok;
        n.code = ToCode(tok);
        ret.push_back(std::make_shared<Node>(n));
      }
    }
    return ret;
  }
};

std::vector<snode> parse(const std::string &s) {
  return Parser(tokenize(s)).parse();
}

environment::environment() : outer(NULL) {}
environment::environment(senvironment outer) : outer(outer) {}

snode environment::get(size_t code) {
  auto found = env.find(code);
  if (found != env.end()) {
    return found->second;
  } else {
    if (outer != NULL) {
      return outer->get(code);
    } else {
      return nil;
    }
  }
}

snode environment::get(snode &k) { return get(k->code); }

snode environment::set(snode &k, snode &v) { return env[k->code] = v; }

snode fn(snode n, std::shared_ptr<environment> outer_env) {
  snode n2(n);
  n2->type = Node::T_FN;
  n2->outer_env = outer_env;
  return n2;
}

std::map<std::string, std::vector<snode>> macros;

snode apply_macro(snode body, std::map<std::string, snode> vars) {
  if (body->type == Node::T_LIST) {
    std::vector<snode> &bvec = body->v_list;
    std::vector<snode> ret;
    for (unsigned int i = 0; i < bvec.size(); i++) {
      snode b = bvec.at(i);
      if (b->v_string == "...") {
        snode &vargs = vars[b->v_string];
        ret.insert(ret.end(), vargs->v_list.begin(), vargs->v_list.end());
      } else
        ret.push_back(apply_macro(bvec.at(i), vars));
    }
    return std::make_shared<Node>(ret);
  } else {
    std::string &bstr = body->v_string;
    if (vars.find(bstr) != vars.end())
      return vars[bstr];
    else
      return body;
  }
}

snode macroexpand(snode n) {
  std::vector<snode> nList = n->v_list;
  if (macros.find(nList[0]->v_string) != macros.end()) {
    std::vector<snode> &macro = macros[nList[0]->v_string];
    std::map<std::string, snode> macrovars;
    std::vector<snode> &argsyms = macro[0]->v_list;
    for (unsigned int i = 0; i < argsyms.size(); i++) {
      std::string argsym = argsyms.at(i)->v_string;
      if (argsym == "...") {
        std::vector<snode> ellipsis;
        snode n2(std::make_shared<Node>(ellipsis));
        std::vector<snode> &ellipsis2 = n2->v_list;
        for (unsigned int i2 = i + 1; i2 < nList.size(); i2++)
          ellipsis2.push_back(nList.at(i2));
        macrovars[argsym] = n2;
        break;
      } else {
        macrovars[argsyms.at(i)->v_string] = nList.at(i + 1);
      }
    }
    return apply_macro(macro[1], macrovars);
  } else
    return n;
}

snode compile(snode &n) {
  switch (n->type) {
    case Node::T_LIST:  // function (FUNCTION ARGUMENT ..)
    {
      if (n->v_list.size() == 0)
        return n;
      snode func = compile(n->v_list[0]);
      if (func->type == Node::T_SYMBOL &&
          func->v_string ==
              "defmacro") {  // (defmacro add (a b) (+ a b)) ; define macro
        std::vector<snode> v;
        v.push_back(n->v_list[2]);
        v.push_back(n->v_list[3]);
        macros[n->v_list[1]->v_string] = v;
        return nil;
      } else if (func->type == Node::T_SYMBOL &&
                 func->v_string == "quote") {  // ignore macro
        return n;
      } else {
        if (macros.find(func->v_string) != macros.end()) {
          snode expanded = macroexpand(n);
          return compile(expanded);
        } else {
          std::vector<snode> r;
          for (std::vector<snode>::iterator i = n->v_list.begin();
               i != n->v_list.end(); i++) {
            r.push_back(compile(*i));
          }
          return std::make_shared<Node>(r);
        }
      }
    }
    default:
      return n;
  }
}

snode eval(snode &n, senvironment &env) {
  switch (n->type) {
    case Node::T_SYMBOL: {
      // snode n2 = env.get(n);
      // if (n2->type != Node::T_NIL) {
      //	return n2;
      // }
      // else {
      //	cerr << "Unknown variable: " << n->v_string << endl;
      //	return nil;
      // }
      return env->get(n);
    }
    case Node::T_LIST:  // function (FUNCTION ARGUMENT ..)
    {
      auto &list = n->v_list;
      if (list.empty())
        return nil;

      snode func = eval(n->v_list[0], env);
      switch (func->type) {
        case Node::T_SPECIAL: {
          return func->v_builtin(n->v_list, env);
        }
        case Node::T_BUILTIN:
        case Node::T_FN: {
          // evaluate arguments
          std::vector<snode> args;
          environment env2(func->outer_env);
          args.reserve(n->v_list.size() - 1);
          for (auto i = n->v_list.begin() + 1; i != n->v_list.end(); i++) {
            args.push_back(eval(*i, env));
          }
          senvironment senv2(std::make_shared<environment>(env2));
          return apply(func, args, senv2);
        }
        default:
          return nil;
      }  // end switch (func->type)
    }
    default:
      return n;
  }
}

std::vector<snode> compile_all(std::vector<snode> &lst) {
  std::vector<snode> compiled;
  for (size_t i = 0; i < lst.size(); i++) {
    compiled.push_back(compile(lst[i]));
  }
  return compiled;
}

snode eval_all(std::vector<snode> &lst) {
  if (lst.empty())
    return nil;
  size_t last = lst.size() - 1;
  for (size_t i = 0; i < last; i++) {
    eval(lst[i], global_env);
  }
  return eval(lst[last], global_env);
}

template <typename T>
void print_map_keys(std::map<std::string, T> &m) {
  int i = 0;
  for (typename std::map<std::string, T>::iterator iter = m.begin();
       iter != m.end(); iter++) {
    printf(" %s", iter->first.c_str());
    i++;
    if (i % 10 == 0)
      puts("");
  }
  puts("");
}

void print_logo() {
  printf(
      "Paren %s (C) 2013-2014 Kim, Taegyoon "
      "(https://bitbucket.org/ktg/paren)\n",
      PAREN_VERSION);
  printf("Predefined Symbols:");
  std::vector<std::string> v;
  for (auto iter = global_env->env.begin(); iter != global_env->env.end();
       iter++) {
    v.push_back(symname[iter->first]);
  }
  sort(v.begin(), v.end());
  for (std::vector<std::string>::iterator iter = v.begin(); iter != v.end();
       iter++) {
    printf(" %s", (*iter).c_str());
  }
  puts("");

  puts("Macros:");
  print_map_keys(macros);
}

void prompt() { printf("> "); }

void prompt2() { printf("  "); }

snode eval_string(std::string &s) {
  std::vector<snode> vec = parse(s);
  std::vector<snode> compiled = compile_all(vec);
  return eval_all(compiled);
}

snode eval_string(const char *s) {
  std::string s2(s);
  return eval_string(s2);
}

inline void eval_print(std::string &s) {
  puts(eval_string(s)->str_with_type().c_str());
}

// read-eval-print loop
void repl() {
  std::string code;
  while (true) {
    if (code.length() == 0)
      prompt();
    else
      prompt2();
    std::string line;
    if (!getline(std::cin, line)) {  // EOF
      eval_print(code);
      return;
    }
    code += '\n' + line;
    Tokenizer t(code);
    t.tokenize();
    if (t.unclosed <= 0) {  // no unmatched parenthesis nor quotation
      eval_print(code);
      code = "";
    }
  }
}

snode get(const char *name) {
  std::string s(name);
  return global_env->get(ToCode(s));
}

void set(const char *name, Node value) {
  std::string s(name);
  global_env->env[ToCode(s)] = std::make_shared<Node>(value);
}

// extracts characters from filename and stores them into str
bool slurp(std::string_view filename, std::string &str) {
  std::ifstream input(filename.data());
  if (!input)
    return false;

  std::stringstream buff;
  buff << input.rdbuf();
  str = buff.str();
  return true;
}

// Opposite of slurp. Writes str to filename.
int spit(std::string_view filename, std::string_view str) {
  std::ofstream output(filename.data());
  if (!output)
    return -1;

  output << str;
  return static_cast<int>(str.size());
}

snode special_def(
    std::vector<snode> &raw_args,
    senvironment &env) {  // (def SYMBOL VALUE) ; set in the current environment
  snode value = std::make_shared<Node>(*eval(raw_args[2], env));
  return env->set(raw_args[1], value);
}

snode special_if(std::vector<snode> &raw_args,
                 senvironment &env) {  // (if CONDITION THEN_EXPR {ELSE_EXPR})
  snode &cond = raw_args[1];
  if (eval(cond, env)->v_bool) {
    return eval(raw_args[2], env);
  } else {
    if (raw_args.size() < 4)
      return nil;
    return eval(raw_args[3], env);
  }
}

snode special_set(std::vector<snode> &raw_args,
                  senvironment &env) {  // (set SYMBOL-OR-PLACE VALUE)
  snode var = eval(raw_args[1], env);
  snode value = std::make_shared<Node>(*eval(raw_args[2], env));
  if (raw_args[1]->type == Node::T_SYMBOL && var == nil) {  // new variable
    return env->set(raw_args[1], value);
  } else {
    *var = *value;
    return var;
  }
}

snode special_fn(
    std::vector<snode> &raw_args,
    senvironment &env) {  // (fn (ARGUMENT ..) BODY) => lexical closure
  snode n2 =
      fn(std::make_shared<Node>(raw_args), std::make_shared<environment>(env));
  return n2;
}

snode builtin_plus(std::vector<snode> &args, senvironment &env) {  // (+ X ..)
  size_t len = args.size();
  if (len <= 0)
    return node_0;
  snode first = args[0];
  if (first->type == Node::T_INT) {
    int sum = first->v_int;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      sum += (*i)->to_int();
    }
    return std::make_shared<Node>(sum);
  } else {
    double sum = first->v_double;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      sum += (*i)->to_double();
    }
    return std::make_shared<Node>(sum);
  }
}

snode builtin_minus(std::vector<snode> &args, senvironment &env) {  // (- X ..)
  size_t len = args.size();
  if (len <= 0)
    return node_0;
  snode first = args[0];
  if (first->type == Node::T_INT) {
    int sum = first->v_int;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      sum -= (*i)->to_int();
    }
    return std::make_shared<Node>(sum);
  } else {
    double sum = first->v_double;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      sum -= (*i)->to_double();
    }
    return std::make_shared<Node>(sum);
  }
}

snode builtin_mul(std::vector<snode> &args, senvironment &env) {  // (* X ..)
  size_t len = args.size();
  if (len <= 0)
    return node_1;
  snode first = args[0];
  if (first->type == Node::T_INT) {
    int sum = first->v_int;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      sum *= (*i)->to_int();
    }
    return std::make_shared<Node>(sum);
  } else {
    double sum = first->v_double;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      sum *= (*i)->to_double();
    }
    return std::make_shared<Node>(sum);
  }
}

snode builtin_div(std::vector<snode> &args, senvironment &env) {  // (/ X ..)
  size_t len = args.size();
  if (len <= 0)
    return node_1;
  snode first = args[0];
  if (first->type == Node::T_INT) {
    int sum = first->v_int;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      sum /= (*i)->to_int();
    }
    return std::make_shared<Node>(sum);
  } else {
    double sum = first->v_double;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      sum /= (*i)->to_double();
    }
    return std::make_shared<Node>(sum);
  }
}

snode builtin_lt(std::vector<snode> &args, senvironment &env) {  // (< X Y)
  if (args[0]->type == Node::T_INT) {
    return std::make_shared<Node>(args[0]->v_int < args[1]->to_int());
  } else {
    return std::make_shared<Node>(args[0]->v_double < args[1]->to_double());
  }
}

snode builtin_caret(std::vector<snode> &args,
                    senvironment &env) {  // (^ BASE EXPONENT)
  return std::make_shared<Node>(
      pow(args[0]->to_double(), args[1]->to_double()));
}

snode builtin_percent(std::vector<snode> &args,
                      senvironment &env) {  // (% DIVIDEND DIVISOR)
  return std::make_shared<Node>(args[0]->to_int() % args[1]->to_int());
}

snode builtin_sqrt(std::vector<snode> &args, senvironment &env) {  // (sqrt X)
  return std::make_shared<Node>(sqrt(args[0]->to_double()));
}

snode builtin_plusplus(std::vector<snode> &args, senvironment &env) {  // (++ X)
  size_t len = args.size();
  if (len <= 0)
    return std::make_shared<Node>(0);
  snode first = args[0];
  if (first->type == Node::T_INT) {
    first->v_int++;
  } else {
    first->v_double++;
  }
  return first;
}

snode builtin_minusminus(std::vector<snode> &args,
                         senvironment &env) {  // (-- X)
  size_t len = args.size();
  if (len <= 0)
    return std::make_shared<Node>(0);
  snode first = args[0];
  if (first->type == Node::T_INT) {
    first->v_int--;
  } else {
    first->v_double--;
  }
  return first;
}

snode builtin_floor(std::vector<snode> &args, senvironment &env) {  // (floor X)
  return std::make_shared<Node>(floor(args[0]->to_double()));
}

snode builtin_ceil(std::vector<snode> &args, senvironment &env) {  // (ceil X)
  return std::make_shared<Node>(ceil(args[0]->to_double()));
}

snode builtin_ln(std::vector<snode> &args, senvironment &env) {  // (ln X)
  return std::make_shared<Node>(log(args[0]->to_double()));
}

snode builtin_log10(std::vector<snode> &args, senvironment &env) {  // (log10 X)
  return std::make_shared<Node>(log10(args[0]->to_double()));
}

snode builtin_rand(std::vector<snode> &args, senvironment &env) {  // (rand)
  return std::make_shared<Node>(rand_double());
}

snode builtin_eqeq(std::vector<snode> &args,
                   senvironment &env) {  // (== X ..) short-circuit
  snode first = args[0];
  if (first->type == Node::T_INT) {
    int firstv = first->v_int;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      if ((*i)->to_int() != firstv) {
        return node_false;
      }
    }
  } else {
    double firstv = first->v_double;
    for (std::vector<snode>::iterator i = args.begin() + 1; i != args.end();
         i++) {
      if ((*i)->to_double() != firstv) {
        return node_false;
      }
    }
  }
  return node_true;
}

snode special_andand(std::vector<snode> &raw_args,
                     senvironment &env) {  // (&& X ..) short-circuit
  for (std::vector<snode>::iterator i = raw_args.begin() + 1;
       i != raw_args.end(); i++) {
    if (!(eval(*i, env))->v_bool) {
      return node_false;
    }
  }
  return node_true;
}

snode special_oror(std::vector<snode> &raw_args,
                   senvironment &env) {  // (|| X ..) short-circuit
  for (std::vector<snode>::iterator i = raw_args.begin() + 1;
       i != raw_args.end(); i++) {
    if ((eval(*i, env))->v_bool) {
      return node_true;
    }
  }
  return node_false;
}

snode builtin_not(std::vector<snode> &args, senvironment &env) {  // (! X)
  return std::make_shared<Node>(!(args[0]->v_bool));
}

snode special_while(std::vector<snode> &raw_args,
                    senvironment &env) {  // (while CONDITION EXPR ..)
  snode &cond = raw_args[1];
  size_t len = raw_args.size();
  while (eval(cond, env)->v_bool) {
    for (size_t i = 2; i < len; i++) {
      eval(raw_args[i], env);
    }
  }
  return nil;
}

snode builtin_strlen(std::vector<snode> &args,
                     senvironment &env) {  // (strlen X)
  return std::make_shared<Node>((int)args[0]->v_string.size());
}

snode builtin_string(std::vector<snode> &args,
                     senvironment &env) {  // (std::string X ..)
  size_t len = args.size();
  if (len <= 1)
    return std::make_shared<Node>(std::string());
  std::string acc;
  for (std::vector<snode>::iterator i = args.begin(); i != args.end(); i++) {
    acc += (*i)->to_string();
  }
  return std::make_shared<Node>(acc);
}

snode builtin_char_at(std::vector<snode> &args,
                      senvironment &env) {  // (char-at X)
  int i = args[1]->to_int();
  assert(i >= 0 && "Negative string indexing");
  return std::make_shared<Node>(args[0]->v_string[static_cast<size_t>(i)]);
}

snode builtin_chr(std::vector<snode> &args, senvironment &env) {  // (chr X)
  char temp[2] = " ";
  temp[0] = (char)args[0]->to_int();
  return std::make_shared<Node>(std::string(temp));
}

void import_impl(const std::string &path) {
  std::string contents;
  if (slurp(path, contents)) {
    eval_string(contents);
  } else {
    std::cerr << "Unable to read file `" << path << "`" << std::endl;
  }
}

// (import X)
snode builtin_import(std::vector<snode> &args, senvironment &env) {
  std::string filename = args[0]->to_string();
  import_impl(filename);
  return nil;
}

snode builtin_double(std::vector<snode> &args,
                     senvironment &env) {  // (double X)
  return std::make_shared<Node>(args[0]->to_double());
}

snode builtin_int(std::vector<snode> &args, senvironment &env) {  // (int X)
  return std::make_shared<Node>(args[0]->to_int());
}

snode builtin_read_string(std::vector<snode> &args,
                          senvironment &env) {  // (read-std::string X)
  return parse(args[0]->to_string())[0];
}

snode builtin_type(std::vector<snode> &args, senvironment &env) {  // (type X)
  return std::make_shared<Node>(args[0]->type_str());
}

snode builtin_eval(std::vector<snode> &args, senvironment &env) {  // (eval X)
  return eval(args[0], env);
}

snode special_quote(std::vector<snode> &raw_args,
                    senvironment &env) {  // (quote X)
  return raw_args[1];
}

snode builtin_list(std::vector<snode> &args,
                   senvironment &env) {  // (list X ..)
  std::vector<snode> ret;
  ret.reserve(args.size());
  for (auto &n : args) {
    ret.push_back(n);
  }
  return std::make_shared<Node>(ret);
}

snode apply(snode &func, std::vector<snode> &args, senvironment &env) {
  if (func->type == Node::T_BUILTIN) {
    return func->v_builtin(args, env);
  } else if (func->type == Node::T_FN) {
    std::vector<snode> &f = func->v_list;
    // anonymous function application-> lexical scoping
    // (fn (ARGUMENT ..) BODY ..)
    std::vector<snode> &arg_syms = f[1]->v_list;

    std::shared_ptr<environment> local_env(
        std::make_shared<environment>(environment(func->outer_env)));

    size_t alen = arg_syms.size();
    for (size_t i = 0; i < alen; i++) {  // assign arguments
      // std::string &k = arg_syms.at(i)->v_string;
      // local_env.env[k] = eval(n->v_list.at(i+1), env);
      snode &k = arg_syms[i];
      local_env->env[k->code] = args[i];
    }

    size_t last = f.size() - 1;
    if (last < 2)
      return nil;
    for (size_t i = 2; i < last; i++) {  // body
      eval(f.at(i), local_env);
    }
    return eval(f[last], local_env);
  } else {
    return nil;
  }
}

snode builtin_apply(std::vector<snode> &args,
                    senvironment &env) {  // (apply FUNC LIST)
  snode func = args[0];
  std::vector<snode> lst = args[1]->v_list;
  return apply(func, lst, env);
}

snode builtin_fold(std::vector<snode> &args,
                   senvironment &env) {  // (fold FUNC LIST)
  snode f = args[0];
  snode lst = args[1];
  snode acc = lst->v_list[0];
  std::vector<snode> args2;
  args2.reserve(2);
  args2.push_back(nil);  // first argument
  args2.push_back(nil);  // second argument
  for (unsigned int i = 1; i < lst->v_list.size(); i++) {
    args2[0] = acc;
    args2[1] = lst->v_list.at(i);
    acc = apply(f, args2, env);
  }
  return acc;
}

snode builtin_map(std::vector<snode> &args,
                  senvironment &env) {  // (std::map FUNC LIST)
  snode f = args[0];
  snode lst = args[1];
  std::vector<snode> acc;
  std::vector<snode> args2;
  auto len = lst->v_list.size();
  acc.reserve(len);
  args2.push_back(nil);
  for (unsigned int i = 0; i < len; i++) {
    args2[0] = lst->v_list[i];
    acc.push_back(apply(f, args2, env));
  }
  return std::make_shared<Node>(acc);
}

snode builtin_filter(std::vector<snode> &args,
                     senvironment &env) {  // (filter FUNC LIST)
  snode f = args[0];
  snode lst = args[1];
  std::vector<snode> acc;
  std::vector<snode> args2;
  args2.push_back(nil);
  for (unsigned int i = 0; i < lst->v_list.size(); i++) {
    snode item = lst->v_list.at(i);
    args2[0] = item;
    snode ret = apply(f, args2, env);
    if (ret->v_bool)
      acc.push_back(item);
  }
  return std::make_shared<Node>(acc);
}

snode builtin_push_backd(
    std::vector<snode> &args,
    senvironment &env) {  // (push-back! LIST ITEM) ; destructive
  args[0]->v_list.push_back(std::make_shared<Node>(*args[1]));
  return args[0];
}

snode builtin_pop_backd(std::vector<snode> &args,
                        senvironment &env) {  // (pop-back! LIST) ; destructive
  auto &v = args[0]->v_list;
  snode n = v.back();
  v.pop_back();
  return n;
}

snode builtin_nth(std::vector<snode> &args,
                  senvironment &env) {  // (nth INDEX LIST)
  int i = args[0]->to_int();
  assert(i >= 0 && "Negative index for list");
  return args[1]->v_list[static_cast<size_t>(i)];
}

snode builtin_length(std::vector<snode> &args,
                     senvironment &env) {  // (length LIST)
  return std::make_shared<Node>((int)args[0]->v_list.size());
}

snode special_begin(std::vector<snode> &raw_args,
                    senvironment &env) {  // (begin X ..)
  size_t last = raw_args.size() - 1;
  if (last <= 0)
    return nil;
  for (size_t i = 1; i < last; i++) {
    eval(raw_args[i], env);
  }
  return eval(raw_args[last], env);
}

snode builtin_pr(std::vector<snode> &args, senvironment &env) {  // (pr X ..)
  std::vector<snode>::iterator first = args.begin();
  for (std::vector<snode>::iterator i = first; i != args.end(); i++) {
    if (i != first)
      printf(" ");
    printf("%s", (*i)->to_string().c_str());
  }
  return nil;
}

snode builtin_prn(std::vector<snode> &args, senvironment &env) {  // (prn X ..)
  builtin_pr(args, env);
  puts("");
  return nil;
}

snode builtin_exit(std::vector<snode> &args, senvironment &env) {  // (exit {X})
  puts("");
  if (args.size() == 0)
    exit(0);
  exit(args[0]->to_int());
  return nil;
}

snode builtin_system(
    std::vector<snode> &args,
    senvironment &env) {  // (system X ..) ; Invokes the command processor to
                          // execute a command.
  std::string cmd;
  for (snode &n : args) {
    cmd += n->to_string();
  }
  return std::make_shared<Node>(system(cmd.c_str()));
}

snode builtin_cons(
    std::vector<snode> &args,
    senvironment &env) {  // (cons X LST): Returns a new list where x is the
                          // first element and lst is the rest.
  snode x = args[0];
  snode lst = args[1];
  std::vector<snode> r;
  r.push_back(x);
  for (std::vector<snode>::iterator i = lst->v_list.begin();
       i != lst->v_list.end(); i++) {
    r.push_back(*i);
  }
  return std::make_shared<Node>(r);
}

snode builtin_read_line(std::vector<snode> &args,
                        senvironment &env) {  // (read-line)
  std::string line;
  if (!getline(std::cin, line)) {  // EOF
    return nil;
  } else {
    return std::make_shared<Node>(line);
  }
}

snode builtin_slurp(std::vector<snode> &args,
                    senvironment &env) {  // (slurp FILENAME)
  std::string filename = args[0]->to_string();
  std::string str;
  if (slurp(filename, str))
    return std::make_shared<Node>(str);
  else
    return nil;
}

snode builtin_spit(std::vector<snode> &args,
                   senvironment &env) {  // (spit FILENAME STRING)
  std::string filename = args[0]->to_string();
  std::string str = args[1]->to_string();
  return std::make_shared<Node>(spit(filename, str));
}

snode special_thread(std::vector<snode> &raw_args,
                     senvironment &env) {  // (std::thread EXPR ..): Creates new
                                           // std::thread and starts it.
  Node n2;
  n2.type = Node::T_THREAD;
  // You can not use std::shared_ptr for std::thread. It is deleted early.
  n2.p_thread = new std::thread([&]() {
    std::vector<snode> exprs(raw_args.begin() + 1,
                             raw_args.end());  // to prevent early deletion and
                                               // keep expressions in memory.
    for (auto &sn : exprs) {
      eval(sn, env);
    }
  });
  return std::make_shared<Node>(n2);
}

snode builtin_join(
    std::vector<snode> &args,
    senvironment &env) {  // (join THREAD): wait for THREAD to end
  snode t = args[0];
  pthread &pt = t->p_thread;
  if (pt != nullptr) {
    pt->join();
    delete pt;
    pt = nullptr;
  }
  return nil;
}

void init() {
  srand((unsigned int)time(0));

  global_env = std::make_shared<environment>(environment());

  global_env->env[ToCode("true")] = std::make_shared<Node>(true);
  global_env->env[ToCode("false")] = std::make_shared<Node>(false);
  global_env->env[ToCode("E")] = std::make_shared<Node>(2.71828182845904523536);
  global_env->env[ToCode("PI")] =
      std::make_shared<Node>(3.14159265358979323846);

  global_env->env[ToCode("def")] = make_special(special_def);
  global_env->env[ToCode("set")] = make_special(special_set);
  global_env->env[ToCode("if")] = make_special(special_if);
  global_env->env[ToCode("fn")] = make_special(special_fn);
  global_env->env[ToCode("begin")] = make_special(special_begin);
  global_env->env[ToCode("while")] = make_special(special_while);
  global_env->env[ToCode("quote")] = make_special(special_quote);
  global_env->env[ToCode("&&")] = make_special(special_andand);
  global_env->env[ToCode("||")] = make_special(special_oror);
  global_env->env[ToCode("std::thread")] =
      std::make_shared<Node>(special_thread);

  global_env->env[ToCode("eval")] = std::make_shared<Node>(builtin_eval);
  global_env->env[ToCode("+")] = std::make_shared<Node>(builtin_plus);
  global_env->env[ToCode("-")] = std::make_shared<Node>(builtin_minus);
  global_env->env[ToCode("*")] = std::make_shared<Node>(builtin_mul);
  global_env->env[ToCode("/")] = std::make_shared<Node>(builtin_div);
  global_env->env[ToCode("<")] = std::make_shared<Node>(builtin_lt);
  global_env->env[ToCode("^")] = std::make_shared<Node>(builtin_caret);
  global_env->env[ToCode("%")] = std::make_shared<Node>(builtin_percent);
  global_env->env[ToCode("sqrt")] = std::make_shared<Node>(builtin_sqrt);
  global_env->env[ToCode("++")] = std::make_shared<Node>(builtin_plusplus);
  global_env->env[ToCode("--")] = std::make_shared<Node>(builtin_minusminus);
  global_env->env[ToCode("floor")] = std::make_shared<Node>(builtin_floor);
  global_env->env[ToCode("ceil")] = std::make_shared<Node>(builtin_ceil);
  global_env->env[ToCode("ln")] = std::make_shared<Node>(builtin_ln);
  global_env->env[ToCode("log10")] = std::make_shared<Node>(builtin_log10);
  global_env->env[ToCode("rand")] = std::make_shared<Node>(builtin_rand);
  global_env->env[ToCode("==")] = std::make_shared<Node>(builtin_eqeq);
  global_env->env[ToCode("<")] = std::make_shared<Node>(builtin_lt);
  global_env->env[ToCode("!")] = std::make_shared<Node>(builtin_not);
  global_env->env[ToCode("strlen")] = std::make_shared<Node>(builtin_strlen);
  global_env->env[ToCode("char-at")] = std::make_shared<Node>(builtin_char_at);
  global_env->env[ToCode("chr")] = std::make_shared<Node>(builtin_chr);
  global_env->env[ToCode("int")] = std::make_shared<Node>(builtin_int);
  global_env->env[ToCode("double")] = std::make_shared<Node>(builtin_double);
  global_env->env[ToCode("std::string")] =
      std::make_shared<Node>(builtin_string);
  global_env->env[ToCode("string")] = std::make_shared<Node>(builtin_string);
  global_env->env[ToCode("read-std::string")] =
      std::make_shared<Node>(builtin_read_string);
  global_env->env[ToCode("type")] = std::make_shared<Node>(builtin_type);
  global_env->env[ToCode("list")] = std::make_shared<Node>(builtin_list);
  global_env->env[ToCode("apply")] = std::make_shared<Node>(builtin_apply);
  global_env->env[ToCode("fold")] = std::make_shared<Node>(builtin_fold);
  global_env->env[ToCode("std::map")] = std::make_shared<Node>(builtin_map);
  global_env->env[ToCode("filter")] = std::make_shared<Node>(builtin_filter);
  global_env->env[ToCode("push-back!")] =
      std::make_shared<Node>(builtin_push_backd);
  global_env->env[ToCode("pop-back!")] =
      std::make_shared<Node>(builtin_pop_backd);
  global_env->env[ToCode("nth")] = std::make_shared<Node>(builtin_nth);
  global_env->env[ToCode("length")] = std::make_shared<Node>(builtin_length);
  global_env->env[ToCode("pr")] = std::make_shared<Node>(builtin_pr);
  global_env->env[ToCode("prn")] = std::make_shared<Node>(builtin_prn);
  global_env->env[ToCode("exit")] = std::make_shared<Node>(builtin_exit);
  global_env->env[ToCode("system")] = std::make_shared<Node>(builtin_system);
  global_env->env[ToCode("cons")] = std::make_shared<Node>(builtin_cons);
  global_env->env[ToCode("read-line")] =
      std::make_shared<Node>(builtin_read_line);
  global_env->env[ToCode("slurp")] = std::make_shared<Node>(builtin_slurp);
  global_env->env[ToCode("spit")] = std::make_shared<Node>(builtin_spit);
  global_env->env[ToCode("join")] = std::make_shared<Node>(builtin_join);
  global_env->env[ToCode("import")] = std::make_shared<Node>(builtin_import);

  char library[] = "library.paren";
  std::string code;
  if (libparen::slurp(library, code)) {
    libparen::eval_string(code);
  } else {
    printf("Error loading %s\n", library);
  }
}

extern "C" void paren_init() { init(); }
extern "C" void paren_eval_string(const char *s) { eval_string(s); }
extern "C" void paren_import(const char *s) { import_impl(s); }

}  // namespace libparen
