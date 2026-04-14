#ifndef T800_RENDERGRAPH_H
#define T800_RENDERGRAPH_H

#include <scene/RenderGraphDescriptor.h>
#include <scene/SceneProp.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class Camera;

namespace t800 {

  class BaseDriver;
  class Texture;
  class PrimitiveInst;

  // Resolved edge: a texture dependency between two passes.
  struct GraphEdge {
    int from_pass;     // index of the producing pass (-1 for built-in textures)
    int to_pass;       // index of the consuming pass
    std::string rt;    // RT name (or "@ssao_noise", "@environment_map")
    int attachment;    // BaseDriver attachment enum value
    int slot;          // texture slot on the consumer
  };

  // Runtime node: a resolved pass with its adjacency.
  struct GraphNode {
    int index;
    const RenderPassDesc* desc;
    int rt_handle;                   // resolved RT handle from BaseDriver (-1 if none)
    std::vector<int> inputs_from;    // indices of passes this depends on
    std::vector<int> outputs_to;     // indices of passes that consume our output
  };

  // The render graph: a DAG of render passes.
  //
  // Nodes are render passes. Edges are texture dependencies.
  // The graph can be loaded from JSON and executed against the engine.
  class RenderGraph {
  public:
    RenderGraph() = default;

    // Load the graph descriptor from JSON and build the DAG.
    bool Load(const std::string& path);

    // Create all render targets declared in the graph.
    // Call after Load(), before Execute().
    void CreateRenderTargets(BaseDriver* driver, const SceneProps& props);

    // Execute all passes in order.
    // meshes/meshCount: the scene's mesh instances
    // quads: the scene's quad instances (quads[0] = fullscreen, quads[7] = final)
    // mainCam/lightCam: scene cameras
    // omniCams: optional array of 6 omni light cameras (can be nullptr)
    void Execute(
      BaseDriver* driver,
      SceneProps& props,
      PrimitiveInst* meshes, int meshCount,
      PrimitiveInst* quads,
      ::Camera* mainCam,
      ::Camera* lightCam,
      ::Camera* omniCams,
      int envMapTexIndex
    );

    // ---- Graph inspection (for future GUI) ----

    const std::vector<GraphNode>& GetNodes() const { return m_nodes; }
    const std::vector<GraphEdge>& GetEdges() const { return m_edges; }
    const RenderGraphDesc& GetDescriptor() const { return m_desc; }

    // Get the RT handle for a named render target.
    int GetRTHandle(const std::string& name) const;

    // Print the graph structure to stdout (for debugging).
    void PrintGraph() const;

    // Runtime pass enable/disable
    void DisablePass(const std::string& name) { m_disabledPasses.insert(name); }
    void EnablePass(const std::string& name)  { m_disabledPasses.erase(name); }
    void SetPassEnabled(const std::string& name, bool enabled) {
      if (enabled) EnablePass(name); else DisablePass(name);
    }

  private:
    std::unordered_set<std::string> m_disabledPasses;
    RenderGraphDesc m_desc;

    // Resolved graph
    std::vector<GraphNode> m_nodes;
    std::vector<GraphEdge> m_edges;
    std::unordered_map<std::string, int> m_rtHandles;  // RT name -> driver RT handle

    // Build the DAG (nodes + edges) from the descriptor.
    void BuildGraph();

    // Resolve a "Source:ATTACHMENT" string into (rt_handle, attachment_enum).
    struct ResolvedTexture {
      int rt_handle;
      int attachment;
      bool is_builtin;       // true for @ssao_noise, @environment_map
      std::string builtin;   // the @name if builtin
    };
    ResolvedTexture ResolveTextureInput(const std::string& source) const;

    // Map string names to engine enum values.
    static int ResolveAttachment(const std::string& name);
    static int ResolveColorFormat(const std::string& name);
    static int ResolveDepthFormat(const std::string& name);
    static ShaderKey ResolveSignature(const std::string& name);
    static int ResolveDepthStencilState(const std::string& name);
    static int ResolveCullFace(const std::string& name);
    static int ResolveBlendState(const std::string& name);

    // Execute a single pass.
    void ExecutePass(
      const GraphNode& node,
      BaseDriver* driver,
      SceneProps& props,
      PrimitiveInst* meshes, int meshCount,
      PrimitiveInst* quads,
      ::Camera* mainCam,
      ::Camera* lightCam,
      ::Camera* omniCams,
      int envMapTexIndex
    );
  };

} // namespace t800

#endif
