#ifndef PROJECT_ACTION_H
#define PROJECT_ACTION_H
#include "playback.h"
#include "project_reference.h"
/* Recreate a reference project by hand: make the project folder, its subfolders in Command Prompt,
   open it in VS Code, and type every text file (copying images and lock files), resuming from the
   last saved checkpoint and recording progress in the profile's project history. */
typedef struct CodingProject {
    const Profile *profile;
    const ProgrammingConfig *config;
    ProjectHistory *history;
    const ProjectReference *reference;
    ProjectPlan *plan;
    uint32_t seed;
    const struct SideAppsConfig *side_apps; /* optional side-app breaks; NULL for none */
    struct SideAppsTimer *apps;             /* set while running; a caller may provide its own */
    /* Set by a run that stopped at a safe point because playback_yield asked it to (a session's
       slot ended or it is winding down): the current line was finished and the file saved. */
    int ended_early;
} CodingProject;
void coding_project_print(const CodingProject *job);
int coding_project_run(Playback *p, CodingProject *job);
#endif
