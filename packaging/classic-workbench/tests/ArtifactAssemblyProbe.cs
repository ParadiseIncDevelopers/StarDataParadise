// Read the actual built CLR assembly identities and their references.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Web.Script.Serialization;

internal static class ArtifactAssemblyProbe
{
  private static int Main(string[] args)
  {
    try
    {
      var names = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(args[1]));
      var legacy = (Dictionary<string, object>)names["libraries"];
      int assemblies = 0, references = 0;
      foreach (string path in Directory.GetFiles(args[0]))
      {
        if (!path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) && !path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) continue;
        Assembly assembly;
        try { assembly = Assembly.ReflectionOnlyLoadFrom(Path.GetFullPath(path)); }
        catch (BadImageFormatException) { continue; }
        ++assemblies;
        if (legacy.ContainsKey(assembly.GetName().Name)) throw new Exception("Old assembly identity: " + path);
        foreach (AssemblyName reference in assembly.GetReferencedAssemblies())
        {
          ++references;
          if (legacy.ContainsKey(reference.Name)) throw new Exception(path + " references " + reference.Name);
        }
      }
      Console.WriteLine("PASS " + assemblies + " CLR assemblies and " + references + " assembly references");
      return 0;
    }
    catch (Exception error) { Console.Error.WriteLine(error); return 1; }
  }
}
