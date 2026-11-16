import sys, typst
try:
    pdf, w = typst.compile_with_warnings("main.typ", output="main.pdf")
    for x in w: print("WARN", getattr(x, "message", x), getattr(x, "trace", ""))
    print("ok")
except typst.TypstError as e:
    print("ERR", e.message if hasattr(e, "message") else e)
    for k in ("hints", "trace"):
        if hasattr(e, k): print(k, getattr(e, k))
