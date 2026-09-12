// src/pch.h を1度だけコンパイルするための空実装。
// このファイル自体はコードを持たず、wikigolf_pch ターゲット経由で
// 他のターゲットが target_precompile_headers(... REUSE_FROM wikigolf_pch)
// によって同じ PCH バイナリを使い回すためだけに存在する。
