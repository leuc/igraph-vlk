#define _GNU_SOURCE
#include "graph_loader.h"
#include "layered_sphere.h"
#include "layout_openord.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
sync_node_positions (GraphData *data)
{
  if (!data->nodes)
    return;
  for (int i = 0; i < data->node_count; i++)
    {
      data->nodes[i].position[0] = (float)MATRIX (data->current_layout, i, 0);
      data->nodes[i].position[1] = (float)MATRIX (data->current_layout, i, 1);
      data->nodes[i].position[2]
          = (igraph_matrix_ncol (&data->current_layout) > 2)
                ? (float)MATRIX (data->current_layout, i, 2)
                : 0.0f;
    }
}

static void
refresh_graph_data (GraphData *data)
{
  data->node_count = igraph_vcount (&data->g);
  data->edge_count = igraph_ecount (&data->g);

  data->props.node_count = (int)data->node_count;
  data->props.edge_count = (int)data->edge_count;

  // Re-calculate basic node properties
  if (data->nodes)
    {
      for (uint32_t i = 0; i < data->node_count; i++)
        if (data->nodes[i].label)
          free (data->nodes[i].label);
      free (data->nodes);
    }
  if (data->edges)
    free (data->edges);

  data->nodes = malloc (sizeof (Node) * data->node_count);
  bool has_node_attr = igraph_cattribute_has_attr (
      &data->g, IGRAPH_ATTRIBUTE_VERTEX, data->node_attr_name);
  bool has_label = igraph_cattribute_has_attr (
      &data->g, IGRAPH_ATTRIBUTE_VERTEX, "label");
  float max_n_val = 0.0f;
  if (has_node_attr)
    {
      for (int i = 0; i < data->node_count; i++)
        {
          float val = (float)VAN (&data->g, data->node_attr_name, i);
          if (val > max_n_val)
            max_n_val = val;
        }
    }

  igraph_vector_int_t coreness;
  igraph_vector_int_init (&coreness, data->node_count);
  igraph_coreness (&data->g, &coreness, IGRAPH_ALL);

  for (int i = 0; i < data->node_count; i++)
    {
      data->nodes[i].color[0] = (float)rand () / RAND_MAX;
      data->nodes[i].color[1] = (float)rand () / RAND_MAX;
      data->nodes[i].color[2] = (float)rand () / RAND_MAX;
      data->nodes[i].size
          = (has_node_attr && max_n_val > 0)
                ? (float)VAN (&data->g, data->node_attr_name, i) / max_n_val
                : 1.0f;
      data->nodes[i].label
          = has_label ? strdup (VAS (&data->g, "label", i)) : NULL;
      igraph_vector_int_t neighbors;
      igraph_vector_int_init (&neighbors, 0);
      igraph_neighbors (&data->g, &neighbors, i, IGRAPH_ALL);
      data->nodes[i].degree = igraph_vector_int_size (&neighbors);
      igraph_vector_int_destroy (&neighbors);
      data->nodes[i].coreness = VECTOR (coreness)[i];
      data->nodes[i].glow = 0.0f;
    }
  igraph_vector_int_destroy (&coreness);
  sync_node_positions (data);

  bool has_edge_attr = igraph_cattribute_has_attr (
      &data->g, IGRAPH_ATTRIBUTE_EDGE, data->edge_attr_name);
  float max_e_val = 0.0f;
  if (has_edge_attr)
    {
      for (int i = 0; i < data->edge_count; i++)
        {
          float val = (float)EAN (&data->g, data->edge_attr_name, i);
          if (val > max_e_val)
            max_e_val = val;
        }
    }
  data->edges = malloc (sizeof (Edge) * data->edge_count);
  for (int i = 0; i < data->edge_count; i++)
    {
      igraph_integer_t from, to;
      igraph_edge (&data->g, i, &from, &to);
      data->edges[i].from = (uint32_t)from;
      data->edges[i].to = (uint32_t)to;
      data->edges[i].size
          = (has_edge_attr && max_e_val > 0)
                ? (float)EAN (&data->g, data->edge_attr_name, i) / max_e_val
                : 1.0f;
    }
}

int
graph_load_graphml (const char *filename, GraphData *data,
                    LayoutType layout_type, const char *node_attr,
                    const char *edge_attr)
{
  igraph_set_attribute_table (&igraph_cattribute_table);
  FILE *fp = fopen (filename, "r");
  if (!fp)
    return -1;
  if (igraph_read_graph_graphml (&data->g, fp, 0) != IGRAPH_SUCCESS)
    {
      fclose (fp);
      return -1;
    }
  fclose (fp);
  igraph_simplify (&data->g, 1, 1, NULL);
  data->graph_initialized = true;
  data->node_attr_name = node_attr ? strdup (node_attr) : strdup ("pagerank");
  data->edge_attr_name
      = edge_attr ? strdup (edge_attr) : strdup ("betweenness");
  data->nodes = NULL;
  data->edges = NULL;
  data->hubs = NULL;
  data->hub_count = 0;

  igraph_matrix_init (&data->current_layout, 0, 0);
  if (layout_type == LAYOUT_OPENORD_3D || layout_type == LAYOUT_RANDOM_3D)
    {
      igraph_layout_random_3d (&data->g, &data->current_layout);
    }
  else
    {
      // Use grid layout as default
      int side = (int)ceil (pow (igraph_vcount (&data->g), 1.0 / 3.0));
      igraph_layout_grid_3d (&data->g, &data->current_layout, side, side);
    }
  data->active_layout = layout_type;

  refresh_graph_data (data);
  return 0;
}

void
graph_filter_degree (GraphData *data, int min_degree)
{
  if (!data->graph_initialized)
    return;
  igraph_vector_int_t vids;
  igraph_vector_int_init (&vids, 0);
  igraph_vector_int_t degrees;
  igraph_vector_int_init (&degrees, 0);
  igraph_degree (&data->g, &degrees, igraph_vss_all (), IGRAPH_ALL,
                 IGRAPH_LOOPS);

  for (int i = 0; i < igraph_vector_int_size (&degrees); i++)
    {
      if (VECTOR (degrees)[i] < min_degree)
        {
          igraph_vector_int_push_back (&vids, i);
        }
    }

  if (igraph_vector_int_size (&vids) > 0)
    {
      printf ("Filtering nodes with degree < %d. Removing %d nodes...\n",
              min_degree, (int)igraph_vector_int_size (&vids));
      igraph_delete_vertices (&data->g, igraph_vss_vector (&vids));

      // Cleanup graph
      igraph_simplify (&data->g, 1, 1, NULL);

      // Reset layout for new graph size
      igraph_matrix_destroy (&data->current_layout);
      igraph_matrix_init (&data->current_layout, 0, 0);
      int side = (int)ceil (pow (igraph_vcount (&data->g), 1.0 / 3.0));
      igraph_layout_grid_3d (&data->g, &data->current_layout, side, side);
      refresh_graph_data (data);
    }
  igraph_vector_int_destroy (&vids);
  igraph_vector_int_destroy (&degrees);
}

void
graph_filter_coreness (GraphData *data, int min_coreness)
{
  if (!data->graph_initialized)
    return;
  igraph_vector_int_t vids;
  igraph_vector_int_init (&vids, 0);
  igraph_vector_int_t coreness;
  igraph_vector_int_init (&coreness, 0);
  igraph_coreness (&data->g, &coreness, IGRAPH_ALL);

  for (int i = 0; i < igraph_vector_int_size (&coreness); i++)
    {
      if (VECTOR (coreness)[i] < min_coreness)
        {
          igraph_vector_int_push_back (&vids, i);
        }
    }

  if (igraph_vector_int_size (&vids) > 0)
    {
      printf ("Filtering nodes with coreness < %d. Removing %d nodes...\n",
              min_coreness, (int)igraph_vector_int_size (&vids));
      igraph_delete_vertices (&data->g, igraph_vss_vector (&vids));
      igraph_simplify (&data->g, 1, 1, NULL);
      data->props.coreness_filter = min_coreness;

      // Reset layout for new graph size
      igraph_matrix_destroy (&data->current_layout);
      igraph_matrix_init (&data->current_layout, 0, 0);
      int side = (int)ceil (pow (igraph_vcount (&data->g), 1.0 / 3.0));
      igraph_layout_grid_3d (&data->g, &data->current_layout, side, side);
      refresh_graph_data (data);

      // Set glow based on coreness
      int max_core = 0;
      for (int i = 0; i < data->node_count; i++)
        if (data->nodes[i].coreness > max_core)
          max_core = data->nodes[i].coreness;
      for (int i = 0; i < data->node_count; i++)
        {
          if (max_core > 0)
            data->nodes[i].glow
                = (float)data->nodes[i].coreness / (float)max_core;
          else
            data->nodes[i].glow = 0.5f;
        }
    }
  igraph_vector_int_destroy (&vids);
  igraph_vector_int_destroy (&coreness);
}

void
graph_layout_step (GraphData *data, LayoutType type, int iterations)
{
  if (!data->graph_initialized)
    return;
  data->active_layout = type;
  switch (type)
    {
    case LAYOUT_FR_3D:
      igraph_layout_fruchterman_reingold_3d (
          &data->g, &data->current_layout, 1, iterations,
          (igraph_real_t)data->node_count, NULL, NULL, NULL, NULL, NULL, NULL,
          NULL);
      break;
    case LAYOUT_KK_3D:
      igraph_layout_kamada_kawai_3d (&data->g, &data->current_layout, 1,
                                     data->node_count * 10, 0.0,
                                     (igraph_real_t)data->node_count, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL);
      break;
    case LAYOUT_RANDOM_3D:
      igraph_layout_random_3d (&data->g, &data->current_layout);
      break;
    case LAYOUT_SPHERE:
      igraph_layout_sphere (&data->g, &data->current_layout);
      break;
    case LAYOUT_GRID_3D:
      {
        int side = (int)ceil (pow (data->node_count, 1.0 / 3.0));
        igraph_layout_grid_3d (&data->g, &data->current_layout, side, side);
        break;
      }
    case LAYOUT_UMAP_3D:
      igraph_layout_umap_3d (&data->g, &data->current_layout, 1, NULL, 0.1,
                             iterations, 0);
      break;
    case LAYOUT_DRL_3D:
      {
        igraph_layout_drl_options_t options;
        igraph_layout_drl_options_init (&options, IGRAPH_LAYOUT_DRL_DEFAULT);
        igraph_layout_drl_3d (&data->g, &data->current_layout, 0, &options,
                              NULL);
        break;
      }
    case LAYOUT_OPENORD_3D:
      {
        if (!data->openord)
          {
            data->openord = malloc (sizeof (OpenOrdContext));
            openord_init (data->openord, data->node_count, 128);
            igraph_layout_random_3d (&data->g, &data->current_layout);
          }
        for (int i = 0; i < iterations; i++)
          {
            if (!openord_step (data->openord, data))
              break;
          }
        break;
      }
    case LAYOUT_LAYERED_SPHERE:
      {
        if (!data->layered_sphere)
          {
            data->layered_sphere = malloc (sizeof (LayeredSphereContext));
            layered_sphere_init (data->layered_sphere, data->node_count);
          }
        for (int i = 0; i < iterations; i++)
          {
            if (!layered_sphere_step (data->layered_sphere, data))
              break;
          }
        break;
      }
    }
  sync_node_positions (data);
}

void
graph_remove_overlaps (GraphData *data, float layoutScale)
{
  if (!data->graph_initialized || data->node_count == 0)
    return;
  float max_radius = 0.0f;
  vec3 min_p = { 1e10, 1e10, 1e10 }, max_p = { -1e10, -1e10, -1e10 };
  for (int i = 0; i < data->node_count; i++)
    {
      float r = 0.5f * data->nodes[i].size;
      if (r > max_radius)
        max_radius = r;
      for (int j = 0; j < 3; j++)
        {
          if (data->nodes[i].position[j] < min_p[j])
            min_p[j] = data->nodes[i].position[j];
          if (data->nodes[i].position[j] > max_p[j])
            max_p[j] = data->nodes[i].position[j];
        }
    }
  float cellSize = (max_radius * 2.0f) + 0.1f;
  int dimX = (int)((max_p[0] - min_p[0]) / cellSize) + 1;
  int dimY = (int)((max_p[1] - min_p[1]) / cellSize) + 1;
  int dimZ = (int)((max_p[2] - min_p[2]) / cellSize) + 1;
  if (dimX * dimY * dimZ > 1000000)
    cellSize *= 2.0f;
  int totalCells = dimX * dimY * dimZ;
  int *head = malloc (sizeof (int) * totalCells);
  int *next = malloc (sizeof (int) * data->node_count);
  memset (head, -1, sizeof (int) * totalCells);
  for (int i = 0; i < data->node_count; i++)
    {
      int cx = (int)((data->nodes[i].position[0] - min_p[0]) / cellSize);
      int cy = (int)((data->nodes[i].position[1] - min_p[1]) / cellSize);
      int cz = (int)((data->nodes[i].position[2] - min_p[2]) / cellSize);
      int cellIdx = cx + cy * dimX + cz * dimX * dimY;
      if (cellIdx >= 0 && cellIdx < totalCells)
        {
          next[i] = head[cellIdx];
          head[cellIdx] = i;
        }
    }
  for (int i = 0; i < data->node_count; i++)
    {
      int cx = (int)((data->nodes[i].position[0] - min_p[0]) / cellSize);
      int cy = (int)((data->nodes[i].position[1] - min_p[1]) / cellSize);
      int cz = (int)((data->nodes[i].position[2] - min_p[2]) / cellSize);
      for (int nx = cx - 1; nx <= cx + 1; nx++)
        {
          for (int ny = cy - 1; ny <= cy + 1; ny++)
            {
              for (int nz = cz - 1; nz <= cz + 1; nz++)
                {
                  if (nx < 0 || nx >= dimX || ny < 0 || ny >= dimY || nz < 0
                      || nz >= dimZ)
                    continue;
                  int cellIdx = nx + ny * dimX + nz * dimX * dimY;
                  int other = head[cellIdx];
                  while (other != -1)
                    {
                      if (i != other)
                        {
                          vec3 diff;
                          glm_vec3_sub (data->nodes[i].position,
                                        data->nodes[other].position, diff);
                          float distSq = glm_vec3_norm2 (diff);
                          float r1 = 0.5f * data->nodes[i].size;
                          float r2 = 0.5f * data->nodes[other].size;
                          float minDist = (r1 + r2);
                          if (distSq < minDist * minDist && distSq > 0.0001f)
                            {
                              float dist = sqrtf (distSq);
                              float overlap = minDist - dist;
                              vec3 move;
                              glm_vec3_scale (diff, 0.5f * overlap / dist,
                                              move);
                              glm_vec3_add (data->nodes[i].position, move,
                                            data->nodes[i].position);
                              glm_vec3_sub (data->nodes[other].position, move,
                                            data->nodes[other].position);
                            }
                        }
                      other = next[other];
                    }
                }
            }
        }
    }
  free (head);
  free (next);
  for (int i = 0; i < data->node_count; i++)
    {
      MATRIX (data->current_layout, i, 0)
          = (igraph_real_t)data->nodes[i].position[0];
      MATRIX (data->current_layout, i, 1)
          = (igraph_real_t)data->nodes[i].position[1];
      if (igraph_matrix_ncol (&data->current_layout) > 2)
        MATRIX (data->current_layout, i, 2)
            = (igraph_real_t)data->nodes[i].position[2];
    }
}

void
graph_cluster (GraphData *data, ClusterType type)
{
  igraph_vector_int_t membership;
  igraph_vector_int_init (&membership, igraph_vcount (&data->g));
  igraph_vector_t weights_vec;
  igraph_vector_t node_weights_vec;
  bool use_weights = (data->edges != NULL && data->edge_attr_name != NULL
                      && strcmp (data->edge_attr_name, "") != 0);
  if (use_weights)
    {
      igraph_vector_init (&weights_vec, data->edge_count);
      for (int i = 0; i < data->edge_count; i++)
        {
          VECTOR (weights_vec)[i] = data->edges[i].size;
        }

      igraph_vector_init (&node_weights_vec, data->node_count);
      igraph_vector_int_t degrees;
      igraph_vector_int_init (&degrees, data->node_count);
      igraph_degree (&data->g, &degrees, igraph_vss_all (), IGRAPH_ALL,
                     IGRAPH_LOOPS);
      for (int i = 0; i < data->node_count; i++)
        {
          VECTOR (node_weights_vec)[i] = (igraph_real_t)VECTOR (degrees)[i];
        }
      igraph_vector_int_destroy (&degrees);
    }
  const igraph_vector_t *weights_ptr = use_weights ? &weights_vec : NULL;
  const igraph_vector_t *node_weights_ptr
      = use_weights ? &node_weights_vec : NULL;

  switch (type)
    {
    case CLUSTER_FASTGREEDY:
      {
        igraph_matrix_int_t m;
        igraph_vector_t mo;
        igraph_matrix_int_init (&m, 0, 0);
        igraph_vector_init (&mo, 0);
        igraph_community_fastgreedy (&data->g, weights_ptr, &m, &mo,
                                     &membership);
        igraph_matrix_int_destroy (&m);
        igraph_vector_destroy (&mo);
        break;
      }
    case CLUSTER_WALKTRAP:
      {
        igraph_matrix_int_t m;
        igraph_vector_t mo;
        igraph_matrix_int_init (&m, 0, 0);
        igraph_vector_init (&mo, 0);
        igraph_community_walktrap (&data->g, weights_ptr, 4, &m, &mo,
                                   &membership);
        igraph_matrix_int_destroy (&m);
        igraph_vector_destroy (&mo);
        break;
      }
    case CLUSTER_LABEL_PROP:
      igraph_community_label_propagation (&data->g, &membership, IGRAPH_ALL,
                                          weights_ptr, NULL, NULL);
      break;
    case CLUSTER_MULTILEVEL:
      {
        igraph_vector_t mo;
        igraph_vector_init (&mo, 0);
        igraph_community_multilevel (&data->g, weights_ptr, 1.0, &membership,
                                     NULL, &mo);
        igraph_vector_destroy (&mo);
        break;
      }
    case CLUSTER_LEIDEN:
      igraph_community_leiden (&data->g, weights_ptr, node_weights_ptr,
                               1.0 / (2.0 * data->edge_count), 0.01, false,
                               100, &membership, NULL, NULL);
      break;
    default:
      break;
    }
  int cluster_count = 0;
  for (int i = 0; i < igraph_vector_int_size (&membership); i++)
    if (VECTOR (membership)[i] > cluster_count)
      cluster_count = VECTOR (membership)[i];
  cluster_count++;
  int *cluster_sizes = calloc (cluster_count, sizeof (int));
  for (int i = 0; i < data->node_count; i++)
    cluster_sizes[VECTOR (membership)[i]]++;
  int max_cluster_size = 0;
  for (int i = 0; i < cluster_count; i++)
    if (cluster_sizes[i] > max_cluster_size)
      max_cluster_size = cluster_sizes[i];

  vec3 *colors = malloc (sizeof (vec3) * cluster_count);
  for (int i = 0; i < cluster_count; i++)
    {
      colors[i][0] = (float)rand () / RAND_MAX;
      colors[i][1] = (float)rand () / RAND_MAX;
      colors[i][2] = (float)rand () / RAND_MAX;
    }
  for (int i = 0; i < data->node_count; i++)
    {
      int c_idx = VECTOR (membership)[i];
      memcpy (data->nodes[i].color, colors[c_idx], 12);
      data->nodes[i].glow
          = (max_cluster_size > 0)
                ? (float)cluster_sizes[c_idx] / (float)max_cluster_size
                : 0.0f;
    }
  if (use_weights)
    {
      igraph_vector_destroy (&node_weights_vec);
      igraph_vector_destroy (&weights_vec);
    }
  free (colors);
  free (cluster_sizes);
  igraph_vector_int_destroy (&membership);
}

void
graph_calculate_centrality (GraphData *data, CentralityType type)
{
  igraph_vector_t result;
  igraph_vector_init (&result, igraph_vcount (&data->g));
  switch (type)
    {
    case CENTRALITY_PAGERANK:
      igraph_pagerank (&data->g, IGRAPH_PAGERANK_ALGO_PRPACK, &result, NULL,
                       igraph_vss_all (), IGRAPH_DIRECTED, 0.85, NULL, NULL);
      break;
    case CENTRALITY_HUB:
      {
        igraph_vector_t a;
        igraph_vector_init (&a, 0);
        igraph_hub_and_authority_scores (&data->g, &result, &a, NULL, true,
                                         NULL, NULL);
        igraph_vector_destroy (&a);
        break;
      }
    case CENTRALITY_AUTHORITY:
      {
        igraph_vector_t h;
        igraph_vector_init (&h, 0);
        igraph_hub_and_authority_scores (&data->g, &h, &result, NULL, true,
                                         NULL, NULL);
        igraph_vector_destroy (&h);
        break;
      }
    case CENTRALITY_BETWEENNESS:
      igraph_betweenness (&data->g, &result, igraph_vss_all (),
                          IGRAPH_DIRECTED, NULL);
      break;
    case CENTRALITY_DEGREE:
      {
        igraph_vector_int_t d;
        igraph_vector_int_init (&d, 0);
        igraph_degree (&data->g, &d, igraph_vss_all (), IGRAPH_ALL,
                       IGRAPH_LOOPS);
        for (int i = 0; i < igraph_vector_int_size (&d); i++)
          VECTOR (result)[i] = (float)VECTOR (d)[i];
        igraph_vector_int_destroy (&d);
        break;
      }
    case CENTRALITY_CLOSENESS:
      {
        igraph_bool_t all_reach;
        igraph_closeness (&data->g, &result, NULL, &all_reach,
                          igraph_vss_all (), IGRAPH_ALL, NULL, true);
        break;
      }
    case CENTRALITY_HARMONIC:
      igraph_harmonic_centrality (&data->g, &result, igraph_vss_all (),
                                  IGRAPH_ALL, NULL, true);
      break;
    case CENTRALITY_EIGENVECTOR:
      igraph_eigenvector_centrality (&data->g, &result, NULL, IGRAPH_DIRECTED,
                                     true, NULL, NULL);
      break;
    case CENTRALITY_STRENGTH:
      igraph_strength (&data->g, &result, igraph_vss_all (), IGRAPH_ALL,
                       IGRAPH_LOOPS, NULL);
      break;
    case CENTRALITY_CONSTRAINT:
      igraph_constraint (&data->g, &result, igraph_vss_all (), NULL);
      break;
    default:
      break;
    }
  igraph_real_t min_v, max_v;
  igraph_vector_minmax (&result, &min_v, &max_v);
  for (int i = 0; i < data->node_count; i++)
    {
      float val = (float)VECTOR (result)[i];
      data->nodes[i].size = (max_v > 0) ? (val / (float)max_v) : 1.0f;
      data->nodes[i].degree = (int)(data->nodes[i].size * 20.0f);
      data->nodes[i].glow = (max_v > 0) ? (val / (float)max_v) : 0.0f;
    }
  igraph_vector_destroy (&result);
}

void
graph_highlight_infrastructure (GraphData *data)
{
  if (!data->graph_initialized)
    return;

  // Reset glow
  for (int i = 0; i < data->node_count; i++)
    data->nodes[i].glow = 0.0f;

  // Articulation points
  igraph_vector_int_t ap;
  igraph_vector_int_init (&ap, 0);
  igraph_articulation_points (&data->g, &ap);
  for (int i = 0; i < igraph_vector_int_size (&ap); i++)
    {
      int v_idx = VECTOR (ap)[i];
      data->nodes[v_idx].glow = 1.0f;
      data->nodes[v_idx].color[0] = 1.0f;
      data->nodes[v_idx].color[1] = 0.2f;
      data->nodes[v_idx].color[2] = 0.2f;
    }
  igraph_vector_int_destroy (&ap);

  // Bridges
  igraph_vector_int_t bridges;
  igraph_vector_int_init (&bridges, 0);
  igraph_bridges (&data->g, &bridges);
  for (int i = 0; i < igraph_vector_int_size (&bridges); i++)
    {
      igraph_integer_t from, to;
      igraph_edge (&data->g, VECTOR (bridges)[i], &from, &to);
      data->nodes[from].glow = 1.0f;
      data->nodes[to].glow = 1.0f;
      // Optionally color nodes connected to bridges differently
      data->nodes[from].color[0] = 1.0f;
      data->nodes[from].color[1] = 0.5f;
      data->nodes[from].color[2] = 0.0f;
      data->nodes[to].color[0] = 1.0f;
      data->nodes[to].color[1] = 0.5f;
      data->nodes[to].color[2] = 0.0f;
    }
  igraph_vector_int_destroy (&bridges);
}

void
graph_generate_hubs (GraphData *data, int num_hubs)
{
  if (data->edge_count == 0)
    return;
  data->hub_count = num_hubs;
  data->hubs = realloc (data->hubs, sizeof (Hub) * num_hubs);

  // Init hubs randomly to node positions
  for (int i = 0; i < num_hubs; i++)
    {
      int n = rand () % data->node_count;
      memcpy (data->hubs[i].position, data->nodes[n].position,
              sizeof (float) * 3);
    }

  // K-Means Clustering on Edge Midpoints (10 Iterations)
  int *counts = calloc (num_hubs, sizeof (int));
  float *sums = calloc (num_hubs * 3, sizeof (float));

  for (int iter = 0; iter < 10; iter++)
    {
      memset (counts, 0, sizeof (int) * num_hubs);
      memset (sums, 0, sizeof (float) * num_hubs * 3);

      for (uint32_t i = 0; i < data->edge_count; i++)
        {
          float mid[3] = { (data->nodes[data->edges[i].from].position[0]
                            + data->nodes[data->edges[i].to].position[0])
                               * 0.5f,
                           (data->nodes[data->edges[i].from].position[1]
                            + data->nodes[data->edges[i].to].position[1])
                               * 0.5f,
                           (data->nodes[data->edges[i].from].position[2]
                            + data->nodes[data->edges[i].to].position[2])
                               * 0.5f };
          int best_hub = 0;
          float best_dist = 1e9f;
          for (int h = 0; h < num_hubs; h++)
            {
              float dx = mid[0] - data->hubs[h].position[0];
              float dy = mid[1] - data->hubs[h].position[1];
              float dz = mid[2] - data->hubs[h].position[2];
              float d = dx * dx + dy * dy + dz * dz;
              if (d < best_dist)
                {
                  best_dist = d;
                  best_hub = h;
                }
            }
          sums[best_hub * 3 + 0] += mid[0];
          sums[best_hub * 3 + 1] += mid[1];
          sums[best_hub * 3 + 2] += mid[2];
          counts[best_hub]++;
        }
      for (int h = 0; h < num_hubs; h++)
        {
          if (counts[h] > 0)
            {
              data->hubs[h].position[0] = sums[h * 3 + 0] / counts[h];
              data->hubs[h].position[1] = sums[h * 3 + 1] / counts[h];
              data->hubs[h].position[2] = sums[h * 3 + 2] / counts[h];
            }
        }
    }
  free (counts);
  free (sums);
}

void
graph_free_data (GraphData *data)
{
  if (data->graph_initialized)
    {
      igraph_destroy (&data->g);
      igraph_matrix_destroy (&data->current_layout);
      data->graph_initialized = false;
    }
  if (data->openord)
    {
      openord_cleanup (data->openord);
      free (data->openord);
      data->openord = NULL;
    }
  if (data->layered_sphere)
    {
      layered_sphere_cleanup (data->layered_sphere);
      free (data->layered_sphere);
      data->layered_sphere = NULL;
    }
  if (data->node_attr_name)
    free (data->node_attr_name);
  if (data->edge_attr_name)
    free (data->edge_attr_name);
  if (data->hubs)
    {
      free (data->hubs);
      data->hubs = NULL;
    }
  for (uint32_t i = 0; i < data->node_count; i++)
    if (data->nodes[i].label)
      free (data->nodes[i].label);
  free (data->nodes);
  free (data->edges);
}
