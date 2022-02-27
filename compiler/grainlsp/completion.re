open Grain;
open Compile;
open Grain_parsing;
open Grain_utils;
open Grain_typed;

// maps Grain types to LSP CompletionItemKind
let rec get_kind = (desc: Types.type_desc) =>
  switch (desc) {
  | TTyVar(_) => Utils.item_kind_completion_variable
  | TTyArrow(_) => Utils.item_kind_completion_function
  | TTyTuple(_) => Utils.item_kind_completion_struct
  | TTyRecord(_) => Utils.item_kind_completion_struct
  | TTyConstr(_) => Utils.item_kind_completion_constructor
  | TTySubst(s) => get_kind(s.desc)
  | TTyLink(t) => get_kind(t.desc)
  | _ => Utils.item_kind_completion_text
  };

let process_resolution =
    (
      ~id,
      ~compiled_code: Stdlib__hashtbl.t(string, Typedtree.typed_program),
      ~cached_code: Stdlib__hashtbl.t(string, Typedtree.typed_program),
      ~documents,
      request,
    ) => {
  // right now we just resolve nothing to clear the client's request
  // In future we may want to send more details back with Graindoc details for example
  Rpc.send_completion(
    ~output=stdout,
    ~id,
    [],
  );
};

let process_completion =
    (
      ~id,
      ~compiled_code: Stdlib__hashtbl.t(string, Typedtree.typed_program),
      ~cached_code: Stdlib__hashtbl.t(string, Typedtree.typed_program),
      ~documents,
      request,
    ) => {
  switch (Utils.get_text_document_uri_and_position(request)) {
  | (Some(uri), Some(line), Some(char)) =>
    let completable = Utils.get_original_text(documents, uri, line, char);
    switch (completable) {
    | Nothing => Rpc.send_completion(stdout, id, [])
    | Lident(text) =>
      if (!Hashtbl.mem(cached_code, uri)) {
        // no compiled code available
        Rpc.send_completion(stdout, id, []);
      } else {
        let compiledCode = Hashtbl.find(cached_code, uri);
        let modules = Env.get_all_modules(compiledCode.env);
        let firstChar = text.[0];

        let completions =
          switch (firstChar) {
          | 'A' .. 'Z' =>
            // autocomplete modules
            if (String.contains(text, '.')) {
              // find module name
              let pos = String.rindex(text, '.');
              let modName = String.sub(text, 0, pos);
              let ident: Ident.t = {name: modName, stamp: 0, flags: 0};
              let mod_ident: Path.t = PIdent(ident);

              // only look up completions for imported modules
              if (!List.exists((m: Ident.t) => m.name == modName, modules)) {
                [];
              } else {
                Utils.get_module_exports(mod_ident, compiledCode);
              };
            } else {
              let modules = Env.get_all_modules(compiledCode.env);
              let types = Env.get_all_type_names(compiledCode.env);

              let converted_modules =
                List.map(
                  (m: Ident.t) => {
                    let item: Rpc.completion_item = {
                      label: m.name,
                      kind: 9,
                      detail: "",
                      documentation: "",
                    };
                    item;
                  },
                  modules,
                );

              let converted_types =
                List.map(
                  (t: Ident.t) => {
                    let item: Rpc.completion_item = {
                      label: t.name,
                      kind: 22,
                      detail: "",
                      documentation: "",
                    };
                    item;
                  },
                  types,
                );

              converted_modules @ converted_types;
            }

          | _ =>
            // Autocompete anything in scope
            let values: list((Ident.t, Types.value_description)) =
              Env.get_all_values(compiledCode.env);

            List.map(
              ((i: Ident.t, l: Types.value_description)) => {
                let item: Rpc.completion_item = {
                  label: i.name,
                  kind: get_kind(l.val_type.desc),
                  detail: Utils.lens_sig(l.val_type, ~env=compiledCode.env),
                  documentation: "",
                };
                item;
              },
              values,
            );
          };

        Rpc.send_completion(stdout, id, completions);
      }
    };

  | _ => Rpc.send_completion(stdout, id, [])
  };
};