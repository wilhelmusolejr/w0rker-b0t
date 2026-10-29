#ifndef PROJECT_MANIFEST_H
#define PROJECT_MANIFEST_H
#include "project_reference.h"
/* profiles/projects.json: the GitHub projects bots recreate, each pinned to one commit so the
   reference never changes under a half-typed file. Clones live in programming_files/<name>. */
typedef struct { wchar_t name[128], url[300]; char commit[41]; } ManifestProject;
typedef struct { ManifestProject *items; size_t count; } ProjectManifest;
int project_manifest_load(const wchar_t *path, ProjectManifest *manifest, char *error, size_t capacity);
int project_manifest_parse(const wchar_t *text, ProjectManifest *manifest, char *error, size_t capacity);
void project_manifest_free(ProjectManifest *manifest);
const ManifestProject *project_manifest_find(const ProjectManifest *manifest, const wchar_t *name);
/* This profile's own fixed order of the list, so bots do not all type the same projects in step. */
void project_manifest_order(const ProjectManifest *manifest, const Profile *profile, size_t *order);
/* Make programming_files/<name> a checkout of the pinned commit, cloning it when missing.
   commit overrides the list's commit (a project already in progress keeps its own). */
int project_fetch(const ProgrammingConfig *config, const ManifestProject *project, const char *commit,
                  char *error, size_t capacity);
/* The project this profile works on now: the one in progress, else the next in its order that is
   not completed or skipped. Fetches and loads it. With record set, projects over
   max_project_characters are marked skipped and a new project is added to the history. */
int project_select(const ProgrammingConfig *config, const ProjectManifest *manifest, const Profile *profile,
                   ProjectHistory *history, int record, ProjectReference *reference, HistoryProject **started,
                   wchar_t destination[MAX_PATH], char *error, size_t capacity);
#endif
