using System;
using System.IO;
using System.Runtime.InteropServices;

internal static class Program
{
    private static int Main(string[] args)
    {
        string conversion = null;
        string outputDirectory = null;
        string inputPath = null;

        for (int index = 0; index < args.Length; index++)
        {
            if (string.Equals(args[index], "--convert-to", StringComparison.OrdinalIgnoreCase) && index + 1 < args.Length)
            {
                conversion = args[++index];
                continue;
            }

            if (string.Equals(args[index], "--outdir", StringComparison.OrdinalIgnoreCase) && index + 1 < args.Length)
            {
                outputDirectory = args[++index];
                continue;
            }

            if (!args[index].StartsWith("-", StringComparison.Ordinal))
                inputPath = args[index];
        }

        if (!string.Equals(conversion, "pdf", StringComparison.OrdinalIgnoreCase) ||
            string.IsNullOrWhiteSpace(outputDirectory) ||
            string.IsNullOrWhiteSpace(inputPath))
            return 2;

        inputPath = Path.GetFullPath(inputPath);
        outputDirectory = Path.GetFullPath(outputDirectory);
        Directory.CreateDirectory(outputDirectory);
        string outputPath = Path.Combine(outputDirectory, Path.GetFileNameWithoutExtension(inputPath) + ".pdf");

        dynamic word = null;
        dynamic document = null;
        try
        {
            Type wordType = Type.GetTypeFromProgID("Word.Application");
            if (wordType == null)
                return 3;
            word = Activator.CreateInstance(wordType);
            word.Visible = false;
            word.DisplayAlerts = 0;
            word.ScreenUpdating = false;
            document = word.Documents.Open(inputPath, false, true);
            document.ExportAsFixedFormat(outputPath, 17);
            return File.Exists(outputPath) && new FileInfo(outputPath).Length > 0 ? 0 : 4;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 5;
        }
        finally
        {
            if (document != null)
            {
                try { document.Close(false); } catch { }
                try { Marshal.FinalReleaseComObject(document); } catch { }
            }
            if (word != null)
            {
                try { word.Quit(); } catch { }
                try { Marshal.FinalReleaseComObject(word); } catch { }
            }
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
    }
}
