#ifndef ARGPARSE_H
#define ARGPARSE_H

#include <any>
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef NDEBUG
#define __ARGPARSE_UNREACHABLE(msg, ...)                                     \
  {                                                                          \
    fprintf(stderr, "%s:%d: %s: UNREACHABLE: " msg "\n", __FILE__, __LINE__, \
            __PRETTY_FUNCTION__ __VA_OPT__(, ) __VA_ARGS__);                 \
    __builtin_trap();                                                        \
  }
#else
#define __ARGPARSE_UNREACHABLE(...) __builtin_unreachable()
#endif

namespace argparse {

enum ActionKind {
  // This just stores the argument’s value. This is the default action.
  STORE,

  // This stores the value specified by the const keyword argument; note that
  // the const keyword argument defaults to None. The 'STORE_CONST' action is
  // most commonly used with optional arguments that specify some sort of flag.
  STORE_CONST,

  // These are special cases of 'STORE_CONST'
  // used for storing the values True and False respectively. In addition, they
  // create default values of False and True respectively
  STORE_TRUE,
  STORE_FALSE,

  APPEND,
};

using list_t = std::vector<std::string>;

class Argument {
 public:
  Argument(std::string_view name, ActionKind action)
      : name_(name), action_(action) {}

  std::string_view getName() const { return name_; }
  ActionKind getAction() const { return action_; }
  bool isStore() const { return action_ == STORE; }
  bool isStoreConst() const {
    return action_ == STORE_CONST || action_ == STORE_TRUE ||
           action_ == STORE_FALSE;
  }
  bool isAppend() const { return action_ == APPEND; }

  const auto &getConstValue() const {
    assert(const_.has_value() && isStoreConst());
    return const_;
  }

  const auto &getDefault() const {
    assert(default_.has_value());
    return default_;
  }

  bool hasDefault() const { return default_.has_value(); }

  Argument &setStoreTrue() {
    action_ = STORE_TRUE;
    const_ = true;
    default_ = false;
    return *this;
  }

  Argument &setAppend() {
    action_ = APPEND;
    return *this;
  }

  Argument &setDefault(std::any def) {
    default_ = def;
    return *this;
  }

  // Alias for `setDefault(std::vector<std::string>())`. This is mainly used
  // when using APPEND but we want a default empty list.
  Argument &setDefaultList() {
    default_ = list_t{};
    return *this;
  }

  std::string_view getHelpText() const { return help_text_; }

 private:
  std::string name_;
  ActionKind action_;
  std::any const_;
  std::any default_;
  std::string help_text_;
};

class Namespace {
 public:
  template <typename T = std::string>
  const T &get(std::string_view arg) const {
    auto found = args_.find(arg);
    assert(found != args_.end());
    return *std::any_cast<T>(&found->second);
  }

  // Short-hand notation for getting an argument with APPEND.
  const auto &getList(std::string_view arg) const {
    return get<std::vector<std::string>>(arg);
  }

  bool has(std::string_view arg) const {
    return args_.find(arg) != args_.end();
  }

  bool HelpIsSet() const { return has("help") && get<bool>("help"); }

 private:
  friend class ArgParser;

  template <typename T = std::string, typename... Args>
  void EmplaceArg(std::string_view name, Args &&...args) {
    auto [it, inserted] = args_.emplace(
        std::piecewise_construct, std::forward_as_tuple(name),
        std::forward_as_tuple(std::make_any<T>(std::move(args)...)));
    assert(inserted && "Duplicate arg name");
  }

  void AddArg(std::string_view name, const std::any &val) {
    auto [it, inserted] = args_.insert(std::pair{name, val});
    assert(inserted && "Duplicate arg name");
  }

  void AppendStringArg(std::string_view name, std::string_view val) {
    auto [it, inserted] = args_.insert(std::pair{name, list_t{}});
    list_t &list = *std::any_cast<list_t>(&it->second);
    list.emplace_back(val);
  }

  std::map<std::string, std::any, std::less<>> args_;
};

class ArgParser {
 public:
  ArgParser(std::string_view progname) : program_name_(progname) {
    // Always add a --help option.
    AddOptArg("help", 'h').setStoreTrue();
  }

  ArgParser() : ArgParser("<program>") {}

  Argument &AddPosArg(std::string_view arg) {
    // NOTE: Use emplace over try_empalce to avoid making a temporary string.
    auto [it, inserted] = known_args_.emplace(
        std::piecewise_construct, std::forward_as_tuple(arg),
        std::forward_as_tuple(arg, STORE));
    assert(inserted && "Duplicate argument");
    pos_args_.push_back(&it->second);
    return it->second;
  }

  Argument &AddOptArg(std::string_view arg) {
    auto [it, inserted] = known_args_.emplace(
        std::piecewise_construct, std::forward_as_tuple(arg),
        std::forward_as_tuple(arg, STORE));
    assert(inserted && "Duplicate argument");
    opt_args_.emplace(std::pair{arg, &it->second});
    return it->second;
  }

  Argument &AddOptArg(std::string_view argname, char short_name) {
    Argument &arg = AddOptArg(argname);
    auto [it, inserted] = short_name_map_.try_emplace(short_name, argname);
    assert(inserted && "Duplicate short name");
    return arg;
  }

  Namespace ParseArgs(int argc, char *const *argv) const {
    assert(argc > 0);

    Namespace ns;
    size_t pos_arg_idx = 0;

    for (int i = 1; i < argc; ++i) {
      std::string_view cmd_arg(argv[i]);
      assert(!cmd_arg.empty());

      if (isOptArg(cmd_arg)) {
        auto optname = getArgName(cmd_arg);
        auto found = opt_args_.find(optname);
        assert(found != opt_args_.end() && "Unknown opt arg.");
        const Argument &arg = *found->second;

        if (arg.isStoreConst()) {
          ns.AddArg(arg.getName(), arg.getConstValue());
        } else if (arg.isAppend()) {
          ++i;  // Advance past this argument.
          ns.AppendStringArg(arg.getName(), argv[i]);
        } else {
          ++i;  // Advance past this argument.
          assert(i < argc && "No optional store argument passed to this.");

          ns.EmplaceArg(arg.getName(), argv[i]);
        }
      } else {
        assert(pos_arg_idx < pos_args_.size() &&
               "More than expected number of pos args.");

        // Positional argument.
        const Argument &arg = *pos_args_.at(pos_arg_idx);
        assert(arg.isStore());
        ++pos_arg_idx;

        ns.EmplaceArg(arg.getName(), cmd_arg);
      }
    }

    // Do another iteration going over args with default values.
    for (auto it = known_args_.begin(); it != known_args_.end(); ++it) {
      const Argument &arg = it->second;
      if (ns.has(arg.getName()))
        continue;

      if (!arg.hasDefault())
        continue;

      ns.AddArg(arg.getName(), arg.getDefault());
    }

    return ns;
  }

  // When printing --help, we shall never exceed this many characters when
  // printing. The only exception to this will be if some inidividual token
  // exceeds this limit.
  static constexpr size_t kLineLimit = 80;

  void PrintHelp() { PrintHelp(std::cerr); }

  void PrintHelp(std::ostream &out) {
    struct HelpPrinter {
      HelpPrinter(std::ostream &err) : err_(err) {}
      ~HelpPrinter() { err_ << "\n"; }

      HelpPrinter &operator<<(std::string_view s) {
        PrintText(s);
        return *this;
      }

      void setPadding(size_t padding) {
        assert(padding < kLineLimit);
        this->padding = padding;
      }

      void PrintText(std::string_view text) {
        size_t start = 0, end = 0;
        for (size_t i = 0; i < text.size(); ++i) {
          char c = text[i];

          if (isspace(c)) {
            // Commit any prior token.
            err_ << text.substr(start, end - start);

            err_ << c;
            if (c == '\n') {
              err_ << std::string(padding, ' ');
              line_pos = padding;
            } else if (++line_pos >= kLineLimit) {
              err_ << "\n";
              err_ << std::string(padding, ' ');
              line_pos = padding;
            }

            // Since this is whitespace, set the start of the token to print the
            // next char in the text.
            start = i + 1;
            end = start;
            continue;
          }

          ++end;
          ++line_pos;
        }

        // Commit any remaining token.
        err_ << text.substr(start, end - start);
      }

      std::ostream &err_;
      size_t line_pos = 0;
      size_t padding = 0;
    };
    HelpPrinter printer(out);

    // Tool usage and description.
    printer << "Usage: " << program_name_;
    printer.setPadding(printer.line_pos);

    for (Argument *pos_arg : pos_args_) {
      printer << " " << pos_arg->getName();
    }
    for (auto it = opt_args_.begin(); it != opt_args_.end(); ++it) {
      printer << " [--" << it->first << "]";
    }

    printer.setPadding(0);
    printer << "\n\n";

    auto print_args = [&]<typename UnaryOp>(auto begin, auto end, UnaryOp op) {
      for (auto it = begin; it != end; ++it) {
        std::string_view arg_name(op(it));
        printer.setPadding(2);
        printer << arg_name;

        const Argument &arg = known_args_.find(arg_name)->second;
        auto help_text = arg.getHelpText();
        if (help_text.empty()) {
          printer << "\n";
          continue;
        }

        printer << ": ";
        printer.setPadding(printer.line_pos);
        printer << help_text << "\n";
      }
    };

    // Positional arguments.
    printer << "positional arguments:";
    printer.setPadding(2);
    printer << "\n";
    print_args(pos_args_.begin(), pos_args_.end(),
               [](auto it) { return (*it)->getName(); });
    printer.setPadding(0);
    printer << "\n";

    // Optional arguments.
    printer << "optional arguments:";
    printer.setPadding(2);
    printer << "\n";
    print_args(opt_args_.begin(), opt_args_.end(),
               [](auto it) -> const std::string & { return it->first; });
  }

 private:
  bool isLongOptArg(std::string_view arg) const {
    return arg.starts_with("--");
  }

  bool isShortOptArg(std::string_view arg) const {
    if (isLongOptArg(arg))
      return false;
    return arg.starts_with('-') && arg.size() == 2;
  }

  bool isOptArg(std::string_view arg) const {
    return isLongOptArg(arg) || isShortOptArg(arg);
  }

  std::string_view getArgName(std::string_view cmd_arg) const {
    if (isShortOptArg(cmd_arg)) {
      // FIXME: We'll need to handle when short options can be joined together.
      // (ex. `tar -xfv ...`)
      auto found = short_name_map_.find(cmd_arg[1]);
      assert(found != short_name_map_.end() && "Unknown short name");
      return found->second;
    }

    if (isLongOptArg(cmd_arg))
      return cmd_arg.substr(2);

    __ARGPARSE_UNREACHABLE("%s is not an optional argument", cmd_arg.data());
  }

  std::string prog_;
  std::string description_;
  const std::string program_name_;

  std::map<std::string, Argument, std::less<>> known_args_;
  std::vector<Argument *> pos_args_;
  std::map<std::string, Argument *, std::less<>> opt_args_;
  std::map<char, std::string> short_name_map_;
};

}  // namespace argparse

#endif  // ARGPARSE_H
