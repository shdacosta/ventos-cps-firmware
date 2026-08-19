Import("env")

# Para testes nativos, compila medicao.cpp como biblioteca
if env.GetProjectOption("platform") == "native":
    # Cria um ambiente com os includes corretos
    medicao_env = env.Clone()
    medicao_env.Append(CPPPATH=["include"])

    # Compila medicao.cpp como uma biblioteca e vincula ao programa
    medicao_lib = medicao_env.BuildLibrary(
        env.subst("$BUILD_DIR/medicao"),
        "src",
        src_filter="+<medicao.cpp>"
    )
    env.Prepend(LIBS=[medicao_lib])
