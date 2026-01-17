#pragma once
#include "dyngen.h"

#include <fstream>
#include <SC_PlugIn.hpp>

// forward declarations
class DynGen;
struct CodeLibrary;
struct DynGenStub;
struct DynGenCallbackData;

extern InterfaceTable *ft;
extern CodeLibrary *gLibrary;

/*! @brief Wraps a DynGen with a ref counter.
 *  RT owned
 *
 *  @discussion because we use async command to delay the vm initialization
 *  of a DynGen it is possible that the server deletes the UGen/DynGen while
 *  we are preparing the vm.
 *  In case we would put the vm in place, this would result in a crash
 *  because the DynGen object has already been deleted by the server.
 *  Therefore, instead of passing around the DynGen itself we pass around
 *  this DynGenStub, which also holds a reference counter.
 *  The ref count starts at 1 and gets incremented during each callback.
 *  When a DynGen gets destroyed, the ref counter gets decremented and its
 *  DynGen reference gets set to nullptr.
 *  When an async command wants to swap the vm,  a nullptr check
 *  can be performed on mObject, indicating if the object is still
 *  existing or not.
 *  Once the reference counter hits 0, the stub can be destroyed, which can
 *  either happen during ~DynGen or within the free function of the async
 *  update command.
 */
struct DynGenStub {
  DynGen* mObject;
  size_t mRefCount;
};

/*! @brief Stores code and associated Dyngen instances
 *  RT managed
 *
 *  @discussion A linked list which manages stores the code under a given ID
 *  (id/code) and also another linked list (DynGen*) which stores all the running
 *  DynGen instances with the associated code, which allows us to update
 *  the running instances in case the code changes.
 *  There is currently no way to delete items from the linked list.
 */
struct CodeLibrary {
  /*! @brief the next entry in the linked list */
  CodeLibrary *next;
  /*! @brief we refer to scripts via ID in order to avoid storing
   *  and sending strings via OSC
   */
  int id;
  /*! @brief  references the first DynGen - all other instances can be accessed
   *  through the double linked list of DynGen
   */
  DynGen *dynGen;
  /*! @brief the eel2 code currently associated with the DynGen instance */
  char* code;
  /*! @brief parameters which need to be exposed by - referenced by the integer
   *  position within the array
   */
  char** parameters;
  int numParameters;
};

/*! @brief A struct to be passed around to update already running dyngen nodes
 */
struct DynGenCallbackData {
  /*! @brief vm is NRT managed - we flip the vm in RT thread, but perpare and
   *  delete it in NRT context
   */
  EEL2Adapter *vm;
  EEL2Adapter *oldVm;

  /*! @brief the running dyngen stub to be updated */
  DynGenStub *dynGenStub;
  /*! @brief the new code to be used */
  const char* code;
  char** parameters;

  /*! @brief vm init */
  uint32 numInputChannels;
  uint32 numOutputChannels;
  uint32 numParameters;

  int sampleRate;
  int blockSize;
  /*! @brief necessary to access params such as sample rate and RTFree */
  World *world;
  /*! @brief necessary for accessing local buffers */
  Graph *parent;
};

/*! @brief The callback payload to enter a new entry into the code library,
 *  which gets invoked via an OSC message/command.
 */
struct NewDynGenLibraryEntry {
  /*! @brief while in sclang land we use strings to identify */
  int hash;

  /*! @discussion This can hold 2 alternatives, both RT managed and 0 terminated
   *  Alternative A: read code from file
   *  absolute path to the file storing the DynGen code
   *  Alternative B: code was bundled within OSC message
   *  RT managed code we receive via script command
   */
  char* oscString;

  /*! @discussion when registering a new script we also need to know which
   *  parameter names we need to expose via the script.
   *  We therefore store an array which matches the position of each
   *  arg from the client so the server only needs to transfer
   *  the array position in its signal instead of transferring
   *  a script
   */
  char** parameterNamesRT;
  int numParameters;

  /*! @brief the newly received code - NRT managed */
  char* code;
  /*! @brief the parameters - NRT version */
  char** parameterNamesNRT;

  /*! @brief the code to be replaced and should be deleted - NRT managed */
  char* oldCode;
  int numOldParameterNames;
  char** oldParameterNames;
};


class Library {
public:
  /*! @brief runs in stage  1 (RT thread)
   *  responds to an osc message on the RT thread - we therefore have to
   *  copy the OSC data to a new struct which then gets passed to another
   *  callback which runs in stage 2 (non-RT thread).
   *  We have to free the created struct afterward via
   *  `pluginCmdCallbackCleanup`.
   */
  static void dyngenAddFileCallback(World *inWorld, void *inUserData,
                                    struct sc_msg_iter *args, void *replyAddr);

  /*! @brief like `dyngenAddFileCallback` but instead of a path we obtain the
   *  script within the OSC message.
   */
  static void addScriptCallback(World *inWorld, void *inUserData,
                                struct sc_msg_iter *args, void *replyAddr);

private:
  /*! @brief unified abstraction layer for dynGenAddFileCallback and
   *  addScriptCallback which preapres the payload for the async callback.
   */
  static void buildGenericPayload(World *inWorld, sc_msg_iter *args, bool isFile);

  /*! @brief performs all cleanup procedures in case RT Alloc of the
   * newLibraryEntry setup fails */
  static void rtCleanup(World *inWorld, NewDynGenLibraryEntry *newLibraryEntry,
                        int numRtParameters);

  /*! @brief this runs in stage 2 (NRT) and copies the content of the
   *  RT owned code to a NRT owned code
   */
  static bool loadScriptToDynGenLibrary(World *world, void *rawCallbackData);

  /*! @brief this runs in stage 2 (NRT) and loads the content of the file
   *  which gets passed to stage 3 (RT)
   */
  static bool loadFileToDynGenLibrary(World *world, void *rawCallbackData);

  /*! @brief runs in stage 3 (RT-thread)
   *
   *  @discussion The code string gets entered into the library
   *  by traversing it as a linked list.
   *  If the hash ID already exists the code gets updated and all running
   *  instances should be updated.
   */
  static bool swapCode(World *world, void *rawCallbackData);

  /*! @brief runs in stage 4 (non-RT-thread) */
  static bool deleteOldCode(World *world, void *rawCallbackData);

  /*! @brief frees the created struct. Uses RTFree since the callback data has
   *  been allocated within RT thread
   */
  static void pluginCmdCallbackCleanup(World *world, void *rawCallbackData);

  /*! @brief a helper method to consume a completion message from the message
   *  stack makes completionMsg either a nullptr (no message) or point
   *  it to the buffer within the osc message.
   */
  static std::pair<int, const char *> getCompletionMsg(sc_msg_iter *args);
};
