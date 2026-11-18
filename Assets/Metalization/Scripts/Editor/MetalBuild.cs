// Player builds: Metalization > Build Windows Player, or from the command line (Tools/AssetPipeline/smoke_test.ps1 -Build):
//   Unity.exe -batchmode -quit -projectPath . -executeMethod Metalization.Editor.MetalBuild.BuildWindowsBatch
using System.Linq;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace Metalization.Editor
{
    public static class MetalBuild
    {
        public const string WindowsPlayer = "Builds/Windows/Metalization.exe";

        [MenuItem("Metalization/Build Windows Player", priority = 20)]
        public static void BuildWindowsMenu() => BuildWindows(true);

        public static bool BuildWindows(bool development)
        {
            MetalLevels.RegisterScenes();
            var options = new BuildPlayerOptions
            {
                scenes = EditorBuildSettings.scenes.Where(s => s.enabled).Select(s => s.path).ToArray(),
                locationPathName = WindowsPlayer,
                target = BuildTarget.StandaloneWindows64,
                options = development ? BuildOptions.Development : BuildOptions.None,
            };
            BuildReport report = BuildPipeline.BuildPlayer(options);
            Debug.Log($"[MZBuild] {report.summary.result}: {report.summary.outputPath} ({report.summary.totalErrors} errors)");
            return report.summary.result == BuildResult.Succeeded;
        }

        /// <summary>Batch-mode entry point (exits with an error code on failure).</summary>
        public static void BuildWindowsBatch()
        {
            if (!BuildWindows(true)) EditorApplication.Exit(1);
        }
    }
}
