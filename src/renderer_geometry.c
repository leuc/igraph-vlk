#include "renderer_geometry.h"
#include "layered_sphere.h"
#include "text.h"
#include "vulkan_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern FontAtlas globalAtlas;

typedef struct
{
  vec3 position;
  float pad1;
  vec3 color;
  float size;
  int degree;
  int pad2, pad3, pad4;
} CompNode;
typedef struct
{
  int sourceId;
  int targetId;
  int elevationLevel;
  int pathLength;
  vec4 path[16];
} CompEdge;
typedef struct
{
  float position[3];
  float pad;
} CompHub;

void
renderer_update_ui (Renderer *r, const char *text)
{
  int len = strlen (text);
  if (len > 1024)
    len = 1024;
  UIInstance instances[1024];
  float xoff = -0.98f;
  float scale = 0.65f;
  for (int i = 0; i < len; i++)
    {
      unsigned char c = text[i];
      CharInfo *ci
          = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
      instances[i].screenPos[0] = xoff;
      instances[i].screenPos[1] = 0.95f;
      instances[i].charRect[0] = ci->x0 * scale;
      instances[i].charRect[1] = ci->y0 * scale;
      instances[i].charRect[2] = ci->x1 * scale;
      instances[i].charRect[3] = ci->y1 * scale;
      instances[i].charUV[0] = ci->u0;
      instances[i].charUV[1] = ci->v0;
      instances[i].charUV[2] = ci->u1;
      instances[i].charUV[3] = ci->v1;
      instances[i].color[0] = 1;
      instances[i].color[1] = 1;
      instances[i].color[2] = 1;
      instances[i].color[3] = 1;
      xoff += (ci->xadvance * scale) / 1720.0f;
    }
  r->uiTextCharCount = len;
  updateBuffer (r->device, r->uiTextInstanceBufferMemory,
                sizeof (UIInstance) * len, instances);
}

void
renderer_update_graph (Renderer *r, GraphData *graph)
{
  vkDeviceWaitIdle (r->device);
  r->nodeCount = graph->node_count;
  r->edgeCount = graph->edge_count;
  if (r->instanceBuffer != VK_NULL_HANDLE)
    {
      vkDestroyBuffer (r->device, r->instanceBuffer, NULL);
      vkFreeMemory (r->device, r->instanceBufferMemory, NULL);
      vkDestroyBuffer (r->device, r->edgeVertexBuffer, NULL);
      vkFreeMemory (r->device, r->edgeVertexBufferMemory, NULL);

      if (r->labelInstanceBuffer != VK_NULL_HANDLE)
        {
          vkDestroyBuffer (r->device, r->labelInstanceBuffer, NULL);
          vkFreeMemory (r->device, r->labelInstanceBufferMemory, NULL);
          r->labelInstanceBuffer = VK_NULL_HANDLE;
        }
      if (r->sphereVertexBuffer != VK_NULL_HANDLE)
        {
          vkDestroyBuffer (r->device, r->sphereVertexBuffer, NULL);
          vkFreeMemory (r->device, r->sphereVertexBufferMemory, NULL);
          r->sphereVertexBuffer = VK_NULL_HANDLE;
        }
      if (r->sphereIndexBuffer != VK_NULL_HANDLE)
        {
          vkDestroyBuffer (r->device, r->sphereIndexBuffer, NULL);
          vkFreeMemory (r->device, r->sphereIndexBufferMemory, NULL);
          r->sphereIndexBuffer = VK_NULL_HANDLE;
        }
      if (r->sphereIndexCounts)
        {
          free (r->sphereIndexCounts);
          r->sphereIndexCounts = NULL;
        }
      if (r->sphereIndexOffsets)
        {
          free (r->sphereIndexOffsets);
          r->sphereIndexOffsets = NULL;
        }
    }

  // Generate Sphere Backgrounds if Layered Layout
  if (graph->active_layout == LAYOUT_LAYERED_SPHERE && graph->layered_sphere
      && graph->layered_sphere->initialized)
    {
      LayeredSphereContext *ctx = graph->layered_sphere;
      r->numSpheres = ctx->num_spheres;
      r->sphereIndexCounts = malloc (sizeof (uint32_t) * r->numSpheres);
      r->sphereIndexOffsets = malloc (sizeof (uint32_t) * r->numSpheres);

      uint32_t totalVertices = 0;
      uint32_t totalIndices = 0;

      // Pass 1: Calculate sizes
      for (int s = 0; s < ctx->num_spheres; s++)
        {
          int target_faces = ctx->grids[s].max_slots;
          int rings = (int)sqrt (target_faces / 8.0);
          if (rings < 4)
            rings = 4;
          if (rings > 32)
            rings = 32; // FIX: Cap sphere resolution to save FPS
          int sectors = rings * 2;

          totalVertices += (rings + 1) * (sectors + 1);
          totalIndices += rings * sectors * 6;
        }

      // Create buffers
      createBuffer (r->device, r->physicalDevice,
                    sizeof (Vertex) * totalVertices,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &r->sphereVertexBuffer, &r->sphereVertexBufferMemory);
      createBuffer (r->device, r->physicalDevice,
                    sizeof (uint32_t) * totalIndices,
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &r->sphereIndexBuffer, &r->sphereIndexBufferMemory);

      Vertex *allVerts = malloc (sizeof (Vertex) * totalVertices);
      uint32_t *allIndices = malloc (sizeof (uint32_t) * totalIndices);

      uint32_t vOffset = 0;
      uint32_t iOffset = 0;

      // Pass 2: Generate geometry
      for (int s = 0; s < ctx->num_spheres; s++)
        {
          double radius = 5.0; // Default
          if (ctx->grids[s].max_slots > 0)
            {
              double x = ctx->grids[s].slots[0].x;
              double y = ctx->grids[s].slots[0].y;
              double z = ctx->grids[s].slots[0].z;
              radius = sqrt (x * x + y * y + z * z);
            }
          float R = (float)radius * r->layoutScale;

          int target_faces = ctx->grids[s].max_slots;
          int rings = (int)sqrt (target_faces / 8.0);
          if (rings < 4)
            rings = 4;
          if (rings > 32)
            rings = 32; // FIX: Cap sphere resolution to save FPS
          int sectors = rings * 2;

          r->sphereIndexOffsets[s] = iOffset;
          uint32_t startIndex = vOffset;

          float const R_inv = 1.f / R;
          float const S = 1.f / (float)sectors;
          float const T = 1.f / (float)rings;

          for (int r_idx = 0; r_idx <= rings; r_idx++)
            {
              float const y = sin (-M_PI_2 + M_PI * r_idx * T);
              float const xz = cos (-M_PI_2 + M_PI * r_idx * T);

              for (int s_idx = 0; s_idx <= sectors; s_idx++)
                {
                  float const x = xz * cos (2 * M_PI * s_idx * S);
                  float const z = xz * sin (2 * M_PI * s_idx * S);

                  vec3 n = { x, y, z };
                  vec3 p;
                  glm_vec3_scale (n, R, p);

                  memcpy (allVerts[vOffset].pos, p, 12);
                  memcpy (allVerts[vOffset].normal, n,
                          12); // Use normal for shading
                  allVerts[vOffset].texCoord[0] = s_idx * S;
                  allVerts[vOffset].texCoord[1] = r_idx * T;
                  vOffset++;
                }
            }

          uint32_t indexCount = 0;
          for (int r_idx = 0; r_idx < rings; r_idx++)
            {
              for (int s_idx = 0; s_idx < sectors; s_idx++)
                {
                  uint32_t curRow = startIndex + r_idx * (sectors + 1);
                  uint32_t nextRow = startIndex + (r_idx + 1) * (sectors + 1);

                  allIndices[iOffset++] = curRow + s_idx;
                  allIndices[iOffset++] = nextRow + s_idx;
                  allIndices[iOffset++] = nextRow + (s_idx + 1);

                  allIndices[iOffset++] = curRow + s_idx;
                  allIndices[iOffset++] = nextRow + (s_idx + 1);
                  allIndices[iOffset++] = curRow + (s_idx + 1);

                  indexCount += 6;
                }
            }
          r->sphereIndexCounts[s] = indexCount;
        }

      updateBuffer (r->device, r->sphereVertexBufferMemory,
                    sizeof (Vertex) * totalVertices, allVerts);
      updateBuffer (r->device, r->sphereIndexBufferMemory,
                    sizeof (uint32_t) * totalIndices, allIndices);
      free (allVerts);
      free (allIndices);
    }
  else
    {
      r->numSpheres = 0;
    }

  createBuffer (r->device, r->physicalDevice, sizeof (Node) * r->nodeCount,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &r->instanceBuffer, &r->instanceBufferMemory);

  int segments = (r->currentRoutingMode == ROUTING_MODE_STRAIGHT) ? 1 : 15;
  r->edgeVertexCount = graph->edge_count * segments * 2;
  createBuffer (r->device, r->physicalDevice,
                sizeof (EdgeVertex) * r->edgeVertexCount,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &r->edgeVertexBuffer, &r->edgeVertexBufferMemory);

  Node *sorted = malloc (sizeof (Node) * graph->node_count);
  uint32_t currentOffset = 0;
  for (int t = 0; t < PLATONIC_COUNT; t++)
    {
      r->platonicDrawCalls[t].firstInstance = currentOffset;
      uint32_t count = 0;
      for (uint32_t i = 0; i < graph->node_count; i++)
        {
          int deg = graph->nodes[i].degree;
          PlatonicType pt;
          if (deg < 4)
            pt = PLATONIC_TETRAHEDRON;
          else if (deg < 6)
            pt = PLATONIC_CUBE;
          else if (deg < 8)
            pt = PLATONIC_OCTAHEDRON;
          else if (deg < 12)
            pt = PLATONIC_DODECAHEDRON;
          else
            pt = PLATONIC_ICOSAHEDRON;
          if (pt == (PlatonicType)t)
            {
              sorted[currentOffset + count] = graph->nodes[i];
              glm_vec3_scale (sorted[currentOffset + count].position,
                              r->layoutScale,
                              sorted[currentOffset + count].position);
              if (sorted[currentOffset + count].size < 0.1f)
                sorted[currentOffset + count].size = 0.1f;
              count++;
            }
        }
      r->platonicDrawCalls[t].count = count;
      currentOffset += count;
    }
  updateBuffer (r->device, r->instanceBufferMemory,
                sizeof (Node) * graph->node_count, sorted);
  EdgeVertex *evs = malloc (sizeof (EdgeVertex) * r->edgeVertexCount);
  uint32_t idx = 0;
  if (r->currentRoutingMode != ROUTING_MODE_STRAIGHT)
    {
      CompNode *cNodes = malloc (sizeof (CompNode) * graph->node_count);
      CompEdge *cEdges = malloc (sizeof (CompEdge) * graph->edge_count);

      for (uint32_t i = 0; i < graph->node_count; i++)
        {
          glm_vec3_scale (graph->nodes[i].position, r->layoutScale,
                          cNodes[i].position);
          cNodes[i].pad1 = 0;
          memcpy (cNodes[i].color, graph->nodes[i].color, sizeof (vec3));
          cNodes[i].size = graph->nodes[i].size;
          cNodes[i].degree = graph->nodes[i].degree;
          cNodes[i].pad2 = cNodes[i].pad3 = cNodes[i].pad4 = 0;
        }
      for (uint32_t i = 0; i < graph->edge_count; i++)
        {
          cEdges[i].sourceId = graph->edges[i].from;
          cEdges[i].targetId = graph->edges[i].to;
          cEdges[i].elevationLevel = 0;
          if (graph->active_layout == LAYOUT_LAYERED_SPHERE
              && graph->layered_sphere && graph->layered_sphere->initialized)
            {
              int s1 = graph->layered_sphere
                           ->node_to_sphere_id[graph->edges[i].from];
              int s2 = graph->layered_sphere
                           ->node_to_sphere_id[graph->edges[i].to];
              cEdges[i].elevationLevel = (s1 != s2) ? 1 : 0;
            }
          cEdges[i].pathLength = 0;
        }

      // --- GPU COMPUTE DISPATCH ---
      VkBuffer nBuf, eBuf, hBuf = VK_NULL_HANDLE;
      VkDeviceMemory nMem, eMem, hMem = VK_NULL_HANDLE;
      createBuffer (r->device, r->physicalDevice,
                    sizeof (CompNode) * graph->node_count,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &nBuf, &nMem);
      createBuffer (r->device, r->physicalDevice,
                    sizeof (CompEdge) * graph->edge_count,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &eBuf, &eMem);
      updateBuffer (r->device, nMem, sizeof (CompNode) * graph->node_count,
                    cNodes);
      updateBuffer (r->device, eMem, sizeof (CompEdge) * graph->edge_count,
                    cEdges);

      CompHub *cHubs = NULL;
      if (r->currentRoutingMode == ROUTING_MODE_3D_HUB_SPOKE
          && graph->hub_count > 0)
        {
          cHubs = malloc (sizeof (CompHub) * graph->hub_count);
          for (int i = 0; i < graph->hub_count; i++)
            {
              memcpy (cHubs[i].position, graph->hubs[i].position,
                      sizeof (float) * 3);
              cHubs[i].pad = 0;
            }
          createBuffer (r->device, r->physicalDevice,
                        sizeof (CompHub) * graph->hub_count,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &hBuf, &hMem);
          updateBuffer (r->device, hMem, sizeof (CompHub) * graph->hub_count,
                        cHubs);
        }
      else
        {
          createBuffer (r->device, r->physicalDevice, sizeof (CompHub),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &hBuf, &hMem);
        }

      VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
      VkDescriptorPoolCreateInfo dpInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &dps
      };
      VkDescriptorPool cPool;
      vkCreateDescriptorPool (r->device, &dpInfo, NULL, &cPool);
      VkDescriptorSetAllocateInfo dsAlloc
          = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, cPool, 1,
              &r->computeDescriptorSetLayout };
      VkDescriptorSet cSet;
      vkAllocateDescriptorSets (r->device, &dsAlloc, &cSet);
      VkDescriptorBufferInfo nbi = { nBuf, 0, VK_WHOLE_SIZE },
                             ebi = { eBuf, 0, VK_WHOLE_SIZE },
                             hbi = { hBuf, 0, VK_WHOLE_SIZE };
      VkWriteDescriptorSet writes[3]
          = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, cSet, 0, 0, 1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &nbi, NULL },
              { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, cSet, 1, 0, 1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &ebi, NULL },
              { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, cSet, 2, 0, 1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &hbi, NULL } };
      vkUpdateDescriptorSets (r->device, 3, writes, 0, NULL);

      VkCommandBufferAllocateInfo cbAlloc
          = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL,
              r->commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
      VkCommandBuffer cBuf;
      vkAllocateCommandBuffers (r->device, &cbAlloc, &cBuf);
      VkCommandBufferBeginInfo bInfo
          = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
              VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
      vkBeginCommandBuffer (cBuf, &bInfo);
      if (r->currentRoutingMode == ROUTING_MODE_SPHERICAL_PCB)
        vkCmdBindPipeline (cBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                           r->computeSphericalPipeline);
      else if (r->currentRoutingMode == ROUTING_MODE_3D_HUB_SPOKE)
        vkCmdBindPipeline (cBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                           r->computeHubSpokePipeline);

      vkCmdBindDescriptorSets (cBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                               r->computePipelineLayout, 0, 1, &cSet, 0, NULL);
      struct
      {
        int maxE;
        float baseR;
        int numHubs;
      } pcVals
          = { graph->edge_count, 5.0f * r->layoutScale, graph->hub_count };
      vkCmdPushConstants (cBuf, r->computePipelineLayout,
                          VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (pcVals),
                          &pcVals);

      vkCmdDispatch (cBuf, (graph->edge_count + 255) / 256, 1, 1);
      vkEndCommandBuffer (cBuf);

      VkSubmitInfo sInfo = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cBuf, 0, NULL
      };
      vkQueueSubmit (r->graphicsQueue, 1, &sInfo, VK_NULL_HANDLE);
      vkQueueWaitIdle (r->graphicsQueue); // Wait for GPU processing

      void *mapped;
      vkMapMemory (r->device, eMem, 0, VK_WHOLE_SIZE, 0, &mapped);
      memcpy (cEdges, mapped, sizeof (CompEdge) * graph->edge_count);
      vkUnmapMemory (r->device, eMem);

      vkDestroyBuffer (r->device, hBuf, NULL);
      vkFreeMemory (r->device, hMem, NULL);
      if (cHubs)
        free (cHubs);

      vkFreeCommandBuffers (r->device, r->commandPool, 1, &cBuf);
      vkDestroyDescriptorPool (r->device, cPool, NULL);
      vkDestroyBuffer (r->device, nBuf, NULL);
      vkFreeMemory (r->device, nMem, NULL);
      vkDestroyBuffer (r->device, eBuf, NULL);
      vkFreeMemory (r->device, eMem, NULL);
      // --- END GPU DISPATCH ---

      for (uint32_t i = 0; i < graph->edge_count; i++)
        {
          // CLAMP PATH LENGTH to prevent Heap Buffer Overflows!
          int pLen = cEdges[i].pathLength;
          if (pLen < 0)
            pLen = 0;
          if (pLen > 16)
            pLen = 16;

          for (int p = 0; p < pLen - 1; p++)
            {
              memcpy (evs[idx].pos, cEdges[i].path[p], 12);
              memcpy (evs[idx].color, graph->nodes[graph->edges[i].from].color,
                      12);
              evs[idx].size = graph->edges[i].size;
              idx++;

              memcpy (evs[idx].pos, cEdges[i].path[p + 1], 12);
              memcpy (evs[idx].color, graph->nodes[graph->edges[i].to].color,
                      12);
              evs[idx].size = graph->edges[i].size;
              idx++;
            }
        }
      free (cNodes);
      free (cEdges);
    }
  else
    {
      for (uint32_t i = 0; i < graph->edge_count; i++)
        {
          vec3 p1, p2;
          glm_vec3_scale (graph->nodes[graph->edges[i].from].position,
                          r->layoutScale, p1);
          glm_vec3_scale (graph->nodes[graph->edges[i].to].position,
                          r->layoutScale, p2);
          memcpy (evs[idx].pos, p1, 12);
          memcpy (evs[idx].color, graph->nodes[graph->edges[i].from].color,
                  12);
          evs[idx].size = graph->edges[i].size;
          idx++;
          memcpy (evs[idx].pos, p2, 12);
          memcpy (evs[idx].color, graph->nodes[graph->edges[i].to].color, 12);
          evs[idx].size = graph->edges[i].size;
          idx++;
        }
    }

  r->edgeVertexCount = idx;

  // PREVENT 0-byte memory maps which crash Vulkan
  if (r->edgeVertexCount > 0)
    {
      updateBuffer (r->device, r->edgeVertexBufferMemory,
                    sizeof (EdgeVertex) * r->edgeVertexCount, evs);
    }
  free (evs);

  uint32_t tc = 0;
  for (uint32_t i = 0; i < r->nodeCount; i++)
    if (graph->nodes[i].label)
      tc += strlen (graph->nodes[i].label);
  r->labelCharCount = tc;
  if (tc > 0)
    {
      createBuffer (r->device, r->physicalDevice, sizeof (LabelInstance) * tc,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &r->labelInstanceBuffer, &r->labelInstanceBufferMemory);
      LabelInstance *li = malloc (sizeof (LabelInstance) * r->labelCharCount);
      uint32_t k = 0;
      for (uint32_t i = 0; i < graph->node_count; i++)
        {
          if (!graph->nodes[i].label)
            continue;
          int len = strlen (graph->nodes[i].label);
          float xoff = 0;
          vec3 pos;
          glm_vec3_scale (graph->nodes[i].position, r->layoutScale, pos);
          for (int j = 0; j < len; j++)
            {
              unsigned char c = graph->nodes[i].label[j];
              CharInfo *ci
                  = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
              memcpy (li[k].nodePos, pos, 12);
              li[k].nodePos[1] += (0.5f * graph->nodes[i].size) + 0.3f;
              li[k].charRect[0] = xoff + ci->x0;
              li[k].charRect[1] = ci->y0;
              li[k].charRect[2] = xoff + ci->x1;
              li[k].charRect[3] = ci->y1;
              li[k].charUV[0] = ci->u0;
              li[k].charUV[1] = ci->v0;
              li[k].charUV[2] = ci->u1;
              li[k].charUV[3] = ci->v1;
              xoff += ci->xadvance;
              k++;
            }
        }
      updateBuffer (r->device, r->labelInstanceBufferMemory,
                    sizeof (LabelInstance) * r->labelCharCount, li);
      free (li);
    }
  else
    {
      r->labelInstanceBuffer = VK_NULL_HANDLE;
    }
  free (sorted);
}
