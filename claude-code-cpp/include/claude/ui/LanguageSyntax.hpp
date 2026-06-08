#pragma once

#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <cctype>

namespace claude {

/// Per-language syntax description for the state-machine highlighter.
struct LanguageSyntax {
    std::vector<std::string> keywords;
    /// String delimiters: {open, close}. For symmetric delimiters, open==close.
    std::vector<std::pair<std::string,std::string>> stringDelimiters;
    /// Block comment delimiters: {open, close}.
    std::vector<std::pair<std::string,std::string>> blockCommentDelimiters;
    bool hasLineComments = false;
    std::string lineCommentStart;

    /// Check if a word is a keyword for this language.
    bool isKeyword(const std::string& word) const {
        for (const auto& kw : keywords) {
            if (kw == word) return true;
        }
        return false;
    }
};

/// Singleton registry of language syntax definitions.
/// Covers 20+ languages with keyword tables, string/comment delimiters.
class LanguageSyntaxRegistry {
public:
    static const LanguageSyntaxRegistry& instance() {
        static LanguageSyntaxRegistry reg;
        return reg;
    }

    /// Look up syntax by language tag (e.g. "python", "cpp", "ruby").
    /// Returns nullptr if the language is not registered.
    const LanguageSyntax* get(const std::string& lang) const {
        auto it = langs_.find(lang);
        if (it != langs_.end()) return &it->second;
        return nullptr;
    }

    /// Check if a language is registered (including aliases).
    const LanguageSyntax* getOrAlias(const std::string& lang) const {
        auto it = langs_.find(lang);
        if (it != langs_.end()) return &it->second;
        auto ai = aliases_.find(lang);
        if (ai != aliases_.end()) return get(ai->second);
        return nullptr;
    }

    /// Check if syntax highlighting is disabled via env var.
    static bool isDisabled() {
        static bool disabled = []() {
            const char* env = std::getenv("CLAUDE_CODE_SYNTAX_HIGHLIGHT");
            return env && std::string(env) == "0";
        }();
        return disabled;
    }

private:
    LanguageSyntaxRegistry() {
        init();
    }

    void add(const std::string& name, LanguageSyntax syntax,
             const std::vector<std::string>& aliases = {}) {
        langs_[name] = std::move(syntax);
        for (const auto& a : aliases) {
            aliases_[a] = name;
        }
    }

    void init() {
        // ── C/C++ ──
        add("cpp", LanguageSyntax{
            .keywords = {
                "auto","break","case","catch","class","const","constexpr","continue",
                "default","delete","do","else","enum","explicit","extern","false","final",
                "for","friend","goto","if","inline","mutable","namespace","new","noexcept",
                "nullptr","operator","override","private","protected","public","register",
                "return","sizeof","static","static_assert","static_cast","struct","switch",
                "template","this","throw","true","try","typedef","typeid","typename",
                "union","using","virtual","volatile","while",
                "#include","#define","#ifdef","#ifndef","#endif","#pragma",
                "void","int","long","double","float","char","bool","unsigned","signed",
                "size_t","string","vector","map","set","optional","expected",
                "unique_ptr","shared_ptr","make_unique","make_shared","std"
            },
            .stringDelimiters = {{"\"","\""}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        }, {"c","c++","hpp","h","cc"});

        // ── Python ──
        add("python", LanguageSyntax{
            .keywords = {
                "and","as","assert","async","await","break","class","continue","def","del",
                "elif","else","except","False","finally","for","from","global","if","import",
                "in","is","lambda","None","nonlocal","not","or","pass","raise","return",
                "True","try","while","with","yield","self","print","range","len","list",
                "dict","set","tuple","str","int","float","bool","type","super","__init__"
            },
            .stringDelimiters = {{"\"\"\"","\"\"\""}, {"'''","'''"}, {"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {},  // Python has no block comments
            .hasLineComments = true,
            .lineCommentStart = "#"
        }, {"py"});

        // ── Rust ──
        add("rust", LanguageSyntax{
            .keywords = {
                "as","async","await","break","const","continue","crate","dyn","else","enum",
                "extern","false","fn","for","if","impl","in","let","loop","match","mod",
                "move","mut","pub","ref","return","self","Self","static","struct","super",
                "trait","true","type","unsafe","use","where","while","Vec","String","Option",
                "Result","Ok","Err","Some","None","Box","Rc","Arc","println","vec","format"
            },
            .stringDelimiters = {{"\"","\""}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        }, {"rs"});

        // ── Go ──
        add("go", LanguageSyntax{
            .keywords = {
                "break","case","chan","const","continue","default","defer","else","fallthrough",
                "for","func","go","goto","if","import","interface","map","package","range",
                "return","select","struct","switch","type","var","true","false","nil","iota",
                "append","cap","close","complex","copy","delete","imag","len","make","new",
                "panic","print","println","real","recover","fmt","string","int","bool","error"
            },
            .stringDelimiters = {{"\"","\""}, {"`","`"}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        }, {"golang"});

        // ── JavaScript / TypeScript ──
        add("javascript", LanguageSyntax{
            .keywords = {
                "async","await","break","case","catch","class","const","continue","debugger",
                "default","delete","do","else","export","extends","false","finally","for",
                "function","if","import","in","instanceof","let","new","null","of","return",
                "static","super","switch","this","throw","true","try","typeof","undefined",
                "var","void","while","with","yield","console","require","module","Promise",
                "React","useState","useEffect","useRef","interface","type","enum","implements"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}, {"`","`"}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        }, {"js","typescript","ts","jsx","tsx"});

        // ── Bash / Shell ──
        add("bash", LanguageSyntax{
            .keywords = {
                "if","then","else","elif","fi","case","esac","for","while","until","do","done",
                "in","function","select","time","coproc","return","exit","break","continue",
                "declare","export","local","readonly","typeset","unset","source","alias","echo",
                "printf","read","cd","pwd","ls","grep","find","awk","sed","sort","uniq","wc",
                "cat","head","tail","mkdir","rm","cp","mv","chmod","chown","sudo","apt","npm",
                "git","docker","curl","wget","set","true","false","test"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {},
            .hasLineComments = true,
            .lineCommentStart = "#"
        }, {"sh","zsh","shell"});

        // ── JSON ──
        add("json", LanguageSyntax{
            .keywords = {"true","false","null"},
            .stringDelimiters = {{"\"","\""}},
            .blockCommentDelimiters = {},
            .hasLineComments = false
        });

        // ── Ruby ──
        add("ruby", LanguageSyntax{
            .keywords = {
                "alias","and","begin","break","case","class","def","defined?","do","else",
                "elsif","end","ensure","false","for","if","in","module","next","nil","not",
                "or","redo","rescue","retry","return","self","super","then","true","undef",
                "unless","until","when","while","yield","require","include","attr_accessor",
                "attr_reader","attr_writer","raise","puts","print","new","lambda","proc"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {{"=begin","=end"}},
            .hasLineComments = true,
            .lineCommentStart = "#"
        }, {"rb"});

        // ── PHP ──
        add("php", LanguageSyntax{
            .keywords = {
                "abstract","and","array","as","break","callable","case","catch","class","clone",
                "const","continue","declare","default","die","do","echo","else","elseif","empty",
                "enddeclare","endfor","endforeach","endif","endswitch","endwhile","eval","exit",
                "extends","final","finally","fn","for","foreach","function","global","goto","if",
                "implements","include","instanceof","interface","isset","list","match","namespace",
                "new","or","print","private","protected","public","require","return","static",
                "switch","throw","trait","try","unset","use","var","while","xor","yield",
                "true","false","null","self","parent","void","int","float","bool","string","array"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
            // Note: PHP also supports # line comments, handled in highlightLine
        });

        // ── Scala ──
        add("scala", LanguageSyntax{
            .keywords = {
                "abstract","case","catch","class","def","do","else","extends","false","final",
                "finally","for","forSome","if","implicit","import","lazy","match","new","null",
                "object","override","package","private","protected","return","sealed","super",
                "this","throw","trait","true","try","type","val","var","while","with","yield",
                "String","Int","Long","Double","Float","Boolean","Unit","List","Map","Option",
                "Some","None","Future","Seq","Set","println","apply"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        });

        // ── C# ──
        add("csharp", LanguageSyntax{
            .keywords = {
                "abstract","as","base","bool","break","byte","case","catch","char","checked",
                "class","const","continue","decimal","default","delegate","do","double","else",
                "enum","event","explicit","extern","false","finally","fixed","float","for",
                "foreach","goto","if","implicit","in","int","interface","internal","is","lock",
                "long","namespace","new","null","object","operator","out","override","params",
                "private","protected","public","readonly","ref","return","sbyte","sealed",
                "short","sizeof","stackalloc","static","string","struct","switch","this",
                "throw","true","try","typeof","uint","ulong","unchecked","unsafe","ushort",
                "using","virtual","void","volatile","while","var","dynamic","async","await",
                "yield","nameof","when","record","init","with"
            },
            .stringDelimiters = {{"@\"","\""}, {"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        }, {"cs","c#"});

        // ── Dart ──
        add("dart", LanguageSyntax{
            .keywords = {
                "abstract","as","assert","async","await","break","case","catch","class","const",
                "continue","covariant","default","deferred","do","dynamic","else","enum",
                "extension","external","factory","false","final","finally","for","Function",
                "get","hide","if","implements","import","in","interface","is","late","library",
                "mixin","new","null","on","operator","part","required","rethrow","return","set",
                "show","static","super","switch","this","throw","true","try","typedef","var",
                "void","while","with","yield","int","double","String","bool","List","Map","Set",
                "Future","Stream","print"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}, {"\"\"\"","\"\"\""}, {"'''","'''"}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        });

        // ── Lua ──
        add("lua", LanguageSyntax{
            .keywords = {
                "and","break","do","else","elseif","end","false","for","function","goto","if",
                "in","local","nil","not","or","repeat","return","then","true","until","while",
                "self","print","pairs","ipairs","tostring","tonumber","type","require","module",
                "table","string","math","io","os","coroutine"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}, {"[[","]]"}},
            .blockCommentDelimiters = {{"--[[","]]"}},
            .hasLineComments = true,
            .lineCommentStart = "--"
        });

        // ── Perl ──
        add("perl", LanguageSyntax{
            .keywords = {
                "my","our","local","state","sub","if","elsif","else","unless","while","until",
                "for","foreach","do","eval","require","use","package","BEGIN","END","print",
                "say","return","die","warn","chomp","chop","length","substr","index","push",
                "pop","shift","unshift","map","grep","sort","reverse","join","split","defined",
                "undef","eq","ne","lt","gt","le","ge","and","or","not","qw","qq","qr","qx",
                "tr","s","m","bless","ref","new","AUTOLOAD","DESTROY","true","false","null"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {},  // Perl has no standard block comments
            .hasLineComments = true,
            .lineCommentStart = "#"
        }, {"pl"});

        // ── R ──
        add("r", LanguageSyntax{
            .keywords = {
                "if","else","repeat","while","function","for","in","next","break","TRUE",
                "FALSE","NULL","Inf","NaN","NA","NA_integer_","NA_real_","NA_complex_",
                "NA_character_","library","require","source","print","cat","paste","nchar",
                "substr","grep","gsub","strsplit","c","list","data.frame","matrix","array",
                "factor","length","dim","names","colnames","rownames","head","tail","summary",
                "mean","sd","var","min","max","range","sum","prod","cumsum","rev","sort",
                "order","which","apply","lapply","sapply","vapply","tapply","mapply","rnorm",
                "dnorm","pnorm","qnorm","lm","glm","plot","return","invisible","function"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {},
            .hasLineComments = true,
            .lineCommentStart = "#"
        });

        // ── Swift ──
        add("swift", LanguageSyntax{
            .keywords = {
                "associatedtype","as","async","await","break","case","catch","class","continue",
                "convenience","default","defer","deinit","didSet","do","else","enum","extension",
                "fallthrough","false","fileprivate","final","for","func","get","guard","if",
                "import","in","indirect","infix","init","inout","internal","is","lazy","let",
                "mutating","nil","none","nonisolated","nonmutating","open","operator","optional",
                "override","postfix","precedence","prefix","private","protocol","public",
                "rethrows","return","self","Self","set","some","static","struct","subscript",
                "super","switch","throw","throws","true","try","typealias","var","weak","where",
                "while","willSet","Int","Double","Float","Bool","String","Character","Array",
                "Dictionary","Set","Optional","Result","URL","Date","print"
            },
            .stringDelimiters = {{"\"","\""}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        });

        // ── YAML ──
        add("yaml", LanguageSyntax{
            .keywords = {"true","false","null","yes","no","on","off"},
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {},
            .hasLineComments = true,
            .lineCommentStart = "#"
        }, {"yml"});

        // ── TOML ──
        add("toml", LanguageSyntax{
            .keywords = {"true","false"},
            .stringDelimiters = {{"\"","\""}, {"'","'"}, {"\"\"\"","\"\"\""}, {"'''","'''"}},
            .blockCommentDelimiters = {},
            .hasLineComments = true,
            .lineCommentStart = "#"
        });

        // ── Dockerfile ──
        add("dockerfile", LanguageSyntax{
            .keywords = {
                "FROM","RUN","CMD","LABEL","MAINTAINER","EXPOSE","ENV","ADD","COPY",
                "ENTRYPOINT","VOLUME","USER","WORKDIR","ARG","ONBUILD","STOPSIGNAL",
                "HEALTHCHECK","SHELL","AS"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {},
            .hasLineComments = true,
            .lineCommentStart = "#"
        }, {"docker"});

        // ── Makefile ──
        add("makefile", LanguageSyntax{
            .keywords = {
                "include","-include","define","endef","ifdef","ifndef","ifeq","ifneq",
                "else","endif","export","unexport","override","private","vpath",".PHONY",
                ".SUFFIXES",".DEFAULT",".PRECIOUS",".INTERMEDIATE",".SECONDARY",".SECONDEXPANSION",
                ".DELETE_ON_ERROR",".IGNORE",".SILENT",".EXPORT_ALL_VARIABLES",".NOTPARALLEL",
                ".ONESHELL",".POSIX","all","clean","install","test","build","run","dist"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {},
            .hasLineComments = true,
            .lineCommentStart = "#"
        }, {"make","mk","gnumake"});

        // ── XML ──
        add("xml", LanguageSyntax{
            .keywords = {},  // XML has no keywords per se
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {{"<!--","-->"}},
            .hasLineComments = false
        }, {"xsl","xslt","svg","xsd","wsdl"});

        // ── HTML ──
        add("html", LanguageSyntax{
            .keywords = {},  // HTML tags handled differently
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {{"<!--","-->"}},
            .hasLineComments = false
        }, {"htm"});

        // ── CSS ──
        add("css", LanguageSyntax{
            .keywords = {
                "important","none","auto","inherit","initial","unset","normal","bold",
                "italic","underline","overline","line-through","block","inline","flex",
                "grid","inline-block","inline-flex","inline-grid","absolute","relative",
                "fixed","sticky","static","hidden","visible","scroll","transparent",
                "currentColor","ease","ease-in","ease-out","ease-in-out","linear",
                "step-start","step-end","infinite","alternate","reverse","both","forwards",
                "backwards","left","right","center","top","bottom","center","baseline",
                "middle","text-top","text-bottom","sub","super","border-box","content-box",
                "cover","contain","round","space","repeat","no-repeat","repeat-x","repeat-y",
                "space-between","space-around","space-evenly","stretch","start","end",
                "column","row","wrap","nowrap","pointer","default","not-allowed","grab",
                "grabbing","col","cols","row","rows","gap","fr","minmax","auto-fill",
                "auto-fit","fit-content","min-content","max-content"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = false
        }, {"scss","sass","less"});

        // ── SQL ──
        add("sql", LanguageSyntax{
            .keywords = {
                "SELECT","FROM","WHERE","INSERT","UPDATE","DELETE","CREATE","DROP","ALTER",
                "TABLE","INDEX","JOIN","LEFT","RIGHT","INNER","OUTER","CROSS","ON","AND","OR",
                "NOT","NULL","PRIMARY","KEY","FOREIGN","REFERENCES","ORDER","BY","GROUP",
                "HAVING","LIMIT","OFFSET","DISTINCT","UNION","ALL","AS","SET","VALUES",
                "INTO","IN","IS","LIKE","BETWEEN","EXISTS","CASE","WHEN","THEN","ELSE","END",
                "ASC","DESC","COUNT","SUM","AVG","MIN","MAX","BEGIN","COMMIT","ROLLBACK",
                "GRANT","REVOKE","IF","VIEW","TRIGGER","PROCEDURE","FUNCTION","RETURN",
                "INTEGER","VARCHAR","TEXT","BOOLEAN","DATE","TIMESTAMP","FLOAT","DOUBLE",
                "DECIMAL","CHAR","BLOB","TRUE","FALSE"
            },
            .stringDelimiters = {{"'","'"}, {"\"","\""}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "--"
        });

        // ── Java ──
        add("java", LanguageSyntax{
            .keywords = {
                "abstract","assert","boolean","break","byte","case","catch","char","class",
                "const","continue","default","do","double","else","enum","extends","final",
                "finally","float","for","goto","if","implements","import","instanceof","int",
                "interface","long","native","new","package","private","protected","public",
                "return","short","static","strictfp","super","switch","synchronized","this",
                "throw","throws","transient","try","void","volatile","while","true","false",
                "null","String","Integer","Long","Double","Float","Boolean","Object","List",
                "Map","Set","ArrayList","HashMap","HashSet","System","Override","FunctionalInterface"
            },
            .stringDelimiters = {{"\"","\""}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        }, {"jar"});

        // ── Kotlin ──
        add("kotlin", LanguageSyntax{
            .keywords = {
                "as","break","class","continue","do","else","false","for","fun","if","in",
                "interface","is","null","object","package","return","super","this","throw",
                "true","try","typealias","val","var","when","while","by","catch","constructor",
                "delegate","dynamic","field","file","finally","get","import","init","it",
                "lazy","override","param","property","receiver","set","setparam","where",
                "abstract","annotation","companion","const","crossinline","data","enum",
                "expect","external","final","infix","inline","inner","internal","lateinit",
                "noinline","open","operator","out","private","protected","public","reified",
                "sealed","suspend","tailrec","vararg","String","Int","Long","Double","Float",
                "Boolean","Unit","List","Map","Set","Array","println"," listOf","mutableListOf"
            },
            .stringDelimiters = {{"\"","\""}, {"\"\"\"","\"\"\""}},
            .blockCommentDelimiters = {{"/*","*/"}},
            .hasLineComments = true,
            .lineCommentStart = "//"
        }, {"kt","kts"});

        // ── Haskell ──
        add("haskell", LanguageSyntax{
            .keywords = {
                "module","where","import","data","type","newtype","class","instance","deriving",
                "if","then","else","case","of","let","in","do","guard","forall","qualified",
                "as","hiding","infix","infixl","infixr","otherwise","True","False","Nothing",
                "Just","Maybe","IO","String","Int","Integer","Float","Double","Char","Bool",
                "return","putStrLn","print","map","filter","foldl","foldr","length","head",
                "tail","init","last","reverse","concat","sum","product","minimum","maximum"
            },
            .stringDelimiters = {{"\"","\""}},
            .blockCommentDelimiters = {{"{-","-}"}},
            .hasLineComments = true,
            .lineCommentStart = "--"
        }, {"hs"});

        // ── Zig ──
        add("zig", LanguageSyntax{
            .keywords = {
                "addrspace","align","allowzero","and","anyframe","anytype","asm","async",
                "await","break","callconv","catch","comptime","const","continue","defer",
                "else","enum","errdefer","error","export","extern","fn","for","if","inline",
                "linksection","noalias","nosuspend","noreturn","noreturn","opaque","or",
                "packed","pub","resume","return","struct","suspend","switch","test","threadlocal",
                "try","undefined","union","unreachable","usingnamespace","var","volatile","while",
                "true","false","null","usize","isize","u8","i8","u16","i16","u32","i32","u64",
                "i64","f32","f64","bool","void","type","anyerror","print","allocator"
            },
            .stringDelimiters = {{"\"","\""}},
            .blockCommentDelimiters = {},  // No block comments in Zig
            .hasLineComments = true,
            .lineCommentStart = "//"
        });

        // ── Elixir ──
        add("elixir", LanguageSyntax{
            .keywords = {
                "after","and","case","catch","cond","def","defp","defmodule","defstruct",
                "defprotocol","defimpl","do","else","end","fn","for","if","in","import",
                "not","or","raise","receive","require","rescue","try","unless","use","when",
                "with","true","false","nil","IO","Enum","Map","List","String","Kernel",
                "Agent","GenServer","Supervisor","Application","Task","Stream","Float",
                "Integer","Atom","Tuple","Range","Regex","URI","Path","File","System",
                "puts","inspect","length","size","to_string","atom_to_string","elem"
            },
            .stringDelimiters = {{"\"","\""}, {"'","'"}, {"\"\"\"","\"\"\""}},
            .blockCommentDelimiters = {},
            .hasLineComments = true,
            .lineCommentStart = "#"
        }, {"ex","exs"});

        // ── Erlang ──
        add("erlang", LanguageSyntax{
            .keywords = {
                "after","begin","case","catch","cond","end","fun","if","let","of","query",
                "receive","when","try","module","export","import","define","include","ifdef",
                "ifndef","else","endif","spec","type","record","behaviour","behavior","ok",
                "error","true","false","undefined","io","lists","maps","ets","dict","sets",
                "gen_server","gen_statem","supervisor","application","length","hd","tl"
            },
            .stringDelimiters = {{"\"","\""}},
            .blockCommentDelimiters = {},
            .hasLineComments = true,
            .lineCommentStart = "%"
        }, {"erl"});

        // ── Julia ──
        add("julia", LanguageSyntax{
            .keywords = {
                "baremodule","begin","break","catch","const","continue","do","else","elseif",
                "end","export","false","finally","for","function","global","if","import",
                "let","local","macro","module","mutable","primitive","quote","return",
                "struct","true","try","using","where","while","nothing","missing","undef",
                "println","print","length","size","eltype","typeof","convert","promote",
                "Int","Float64","Float32","Bool","String","Char","Vector","Matrix","Array",
                "Dict","Set","Tuple","Range","UnitRange","AbstractArray","AbstractVector",
                "Any","Nothing","Missing","Some","nothing"
            },
            .stringDelimiters = {{"\"","\""}, {"\"\"\"","\"\"\""}},
            .blockCommentDelimiters = {{"#=","=#"}},
            .hasLineComments = true,
            .lineCommentStart = "#"
        });
    }

    std::unordered_map<std::string, LanguageSyntax> langs_;
    std::unordered_map<std::string, std::string> aliases_;
};

} // namespace claude
