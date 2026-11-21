// dacq: SSDT's own name resolution (DacFx), as a comparison for the
// prototype's resolver.  Test databases only.
//   dacq extract DB OUT.dacpac        model of a local test database
//   dacq refs MODEL.dacpac < SQL      statements split by "go" lines; each
//     is wrapped in a procedure and added to the model; prints per
//     statement "S n", "E number text" for each validation error, and
//     "C schema.table.column" for each column it references
using Microsoft.SqlServer.Dac;
using Microsoft.SqlServer.Dac.Model;

if (args[0] == "extract") {
    var svc = new DacServices($"Server=localhost;User Id=sa;Password=Homeros_Test1;TrustServerCertificate=true;Database={args[1]}");
    svc.Extract(args[2], args[1], args[1], new Version(1, 0), null, null, new DacExtractOptions { ExtractAllTableData = false, VerifyExtraction = false, IgnoreExtendedProperties = true, IgnorePermissions = true, IgnoreUserLoginMappings = true });
    return;
}
using var model = TSqlModel.LoadFromDacpac(args[1], new ModelLoadOptions(DacSchemaModelStorageType.Memory, true));
var stmts = Console.In.ReadToEnd().Replace("\r\n", "\n").Split("\ngo\n");
for (int i = 0; i < stmts.Length; i++)
    if (stmts[i].Trim().Length > 0)
        model.AddOrUpdateObjects($"create procedure dbo.dacq_{i} as\n{stmts[i]}\n", $"q{i}", new TSqlObjectOptions());
var errs = model.Validate();
for (int i = 0; i < stmts.Length; i++) {
    if (stmts[i].Trim().Length == 0) continue;
    Console.WriteLine($"S {i}");
    foreach (var e in errs.Where(e => e.Message.Contains($"[dacq_{i}]")))
        Console.WriteLine($"E {e.Number} {e.Message.Replace('\n', ' ')}");
    var p = model.GetObject(Procedure.TypeClass, new ObjectIdentifier("dbo", $"dacq_{i}"), DacQueryScopes.All);
    if (p == null) continue;
    foreach (var c in p.GetReferenced(DacQueryScopes.All).Where(o => o.ObjectType == Column.TypeClass).Select(o => string.Join(".", o.Name.Parts)).Distinct().OrderBy(s => s))
        Console.WriteLine($"C {c}");
}
