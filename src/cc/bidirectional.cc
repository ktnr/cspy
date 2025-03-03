#include "bidirectional.h"

#include <algorithm> // sort, all_of, find

#include "preprocessing.h" // lowerBoundWeight, getCriticalRes, INF

namespace bidirectional {

/* Public methods */

BiDirectional::BiDirectional(
    const int&                 number_vertices,
    const int&                 number_edges,
    const int&                 source_id,
    const int&                 sink_id,
    const std::vector<double>& max_res_in,
    const std::vector<double>& min_res_in)
    : max_res(max_res_in),
      min_res(min_res_in),
      // Private pointer initialisations
      params_ptr_(std::make_unique<bidirectional::Params>()),
      graph_ptr_(std::make_unique<DiGraph>(
          number_vertices,
          number_edges,
          source_id,
          sink_id)),
      fwd_search_ptr_(std::make_unique<bidirectional::Search>(FWD)),
      bwd_search_ptr_(std::make_unique<bidirectional::Search>(BWD)) {}

std::vector<int> BiDirectional::getPath() const {
  return best_label_->partial_path;
}

std::vector<double> BiDirectional::getConsumedResources() const {
  return best_label_->resource_consumption;
}

double BiDirectional::getTotalCost() const {
  return best_label_->weight;
}

void BiDirectional::checkCriticalRes() const {
  const std::vector<double>& res      = best_label_->resource_consumption;
  double                     min_diff = INF;
  int                        min_r    = 0;
  for (int r = 0; r < res.size(); r++) {
    const double& diff = max_res[r] - res[r];
    if (diff < min_diff) {
      min_diff = diff;
      min_r    = r;
    }
  }
  if (min_r != params_ptr_->critical_res) {
    std::cout << "Critical resource " << params_ptr_->critical_res
              << " does not match final tighest res " << min_r << "\n";
  }
}

void BiDirectional::run() {
  start_time_ = std::chrono::system_clock::now();
  init();

  while (fwd_search_ptr_->stop == false || bwd_search_ptr_->stop == false) {
    const Directions& direction = getDirection();
    if (direction != NODIR) {
      move(direction);
    } else {
      break;
    }
    if (terminate(direction)) {
      break;
    }
  }
  postProcessing();
}

/* Private methods */

/* Preprocessing */

void BiDirectional::runPreprocessing() {
  if (params_ptr_->direction == BOTH && params_ptr_->find_critical_res) {
    const int c = getCriticalRes(max_res, *graph_ptr_);
    std::cout << "c = " << c << "\n";
    setCriticalRes(c);
  }
  if (params_ptr_->bounds_pruning) {
    if (params_ptr_->direction == BOTH || params_ptr_->direction == FWD) {
      lowerBoundWeight(
          fwd_search_ptr_->lower_bound_weight.get(), *graph_ptr_, true);
    }
    if (params_ptr_->direction == BOTH || params_ptr_->direction == BWD) {
      lowerBoundWeight(
          bwd_search_ptr_->lower_bound_weight.get(), *graph_ptr_, false);
    }
  }
}

void BiDirectional::init() {
  // Initialise labels
  labelling::Label label(0.0, {-1, -1}, {}, {}, params_ptr_.get());
  best_label_ = std::make_shared<labelling::Label>(label);
  // Initialise resource bounds
  initResourceBounds();
  // Init individual searches
  initContainers();
  if (params_ptr_->direction == BOTH || params_ptr_->direction == FWD) {
    initSearch(FWD);
  }
  if (params_ptr_->direction == BOTH || params_ptr_->direction == BWD) {
    initSearch(BWD);
  }
  // run preprocessing
  runPreprocessing();
  // Init labels
  if (params_ptr_->direction == BOTH || params_ptr_->direction == FWD) {
    initLabels(FWD);
  }
  if (params_ptr_->direction == BOTH || params_ptr_->direction == BWD) {
    initLabels(BWD);
  }
}

void BiDirectional::initSearch(const Directions& direction) {
  Search* search_ptr = getSearchPtr(direction);
  // Allocate memory
  search_ptr->lower_bound_weight->resize(graph_ptr_->number_vertices, 0.0);
  search_ptr->efficient_labels.resize(graph_ptr_->number_vertices);
  search_ptr->best_labels.resize(graph_ptr_->number_vertices, nullptr);
}

void BiDirectional::initResourceBounds() {
  max_res_curr_ = max_res;
  // If not all lower bounds are 0, initialise variable min_res_curr to
  // vector of 0s
  bool zeros = std::all_of(
      min_res.begin(), min_res.end(), [](const double& d) { return d == 0.0; });
  if (zeros == false) {
    std::vector<double> temp(min_res.size(), 0.0);
    min_res_curr_ = temp;
  } else {
    min_res_curr_ = min_res;
  }
}

void BiDirectional::initLabels(const Directions& direction) {
  Vertex              vertex;
  std::vector<double> res = min_res_curr_;
  std::vector<int>    path;
  Search*             search_ptr = getSearchPtr(direction);

  if (direction == FWD) {
    vertex = graph_ptr_->source;
  } else { // backward
    // set monotone resource to upper bound
    res[params_ptr_->critical_res] = max_res_curr_[params_ptr_->critical_res];
    vertex                         = graph_ptr_->sink;
  }
  // Current label init
  path = {vertex.user_id};
  labelling::Label lab(0.0, vertex, res, path, params_ptr_.get());
  search_ptr->replaceCurrentLabel(lab);
  // Final label dummy init
  Vertex dum_vertex = {-1, -1};
  res               = {};
  path              = {};
  labelling::Label lab2(0.0, dum_vertex, res, path, params_ptr_.get());

  search_ptr->replaceIntermediateLabel(lab2);
  search_ptr->pushHeap();
  // Add to efficient and best labels
  search_ptr->pushEfficientLabel(vertex.lemon_id, *search_ptr->current_label);
  search_ptr->replaceBestLabel(vertex.lemon_id, *search_ptr->current_label);
  search_ptr->addVisitedVertex(vertex.lemon_id);
}

void BiDirectional::initContainers() {
  if (params_ptr_->direction != BOTH) {
    Search* search_ptr = getSearchPtr(params_ptr_->direction);
    search_ptr->makeHeap();
  } else {
    fwd_search_ptr_->makeHeap();
    bwd_search_ptr_->makeHeap();
  }
}

/* Search */

Directions BiDirectional::getDirection() const {
  if (params_ptr_->direction == BOTH) {
    if (!fwd_search_ptr_->stop && bwd_search_ptr_->stop) {
      return FWD;
    } else if (fwd_search_ptr_->stop && !bwd_search_ptr_->stop) {
      return BWD;
    } else if (!fwd_search_ptr_->stop && !bwd_search_ptr_->stop) {
      // TODO: fix random
      // if (method == "random") {
      //   // return a random direction
      //   const std::vector<std::string> directions = {forward,
      //   backward}; const int                      r          =
      //   std::rand() % 2; const std::string&             direction  =
      //   directions[r]; return direction;
      // } else
      if (params_ptr_->method == "generated") {
        // return direction with least number of generated labels
        if (fwd_search_ptr_->generated_count <
            bwd_search_ptr_->generated_count) {
          return FWD;
        }
        return BWD;
      } else if (params_ptr_->method == "processed") {
        // return direction with least number of processed labels
        if (fwd_search_ptr_->processed_count <
            bwd_search_ptr_->processed_count) {
          return FWD;
        }
        return BWD;
      } else if (params_ptr_->method == "unprocessed") {
        // return direction with least number of unprocessed labels
        if (fwd_search_ptr_->unprocessed_count <
            bwd_search_ptr_->unprocessed_count) {
          return FWD;
        }
        return BWD;
      }
    } else {
      ;
    }
  } else {
    // Single direction
    if (params_ptr_->direction == FWD && fwd_search_ptr_->stop) {
      ;
    } else if (params_ptr_->direction == BWD && bwd_search_ptr_->stop) {
      ;
    } else {
      return params_ptr_->direction;
    }
  }
  return NODIR;
}

void BiDirectional::move(const Directions& direction) {
  Search*     search_ptr      = getSearchPtr(direction);
  const bool& bounds_exceeded = checkBounds(direction);
  if (!bounds_exceeded) {
    extendCurrentLabel(direction);
    saveCurrentBestLabel(direction);
  } else {
    search_ptr->stop = true;
  }
  updateHalfWayPoints(direction);
  updateCurrentLabel(direction);
  ++search_ptr->processed_count;
  ++iteration_;
}

bool BiDirectional::terminate(const Directions& direction) {
  // Check time elapsed (if relevant)
  Search* search_ptr = getSearchPtr(direction);
  return terminate(direction, *search_ptr->intermediate_label);
}

bool BiDirectional::terminate(
    const Directions&       direction,
    const labelling::Label& label) {
  // Check time elapsed (if relevant)
  std::chrono::duration<double> duration =
      (std::chrono::system_clock::now() - start_time_);
  double timediff_sec = duration.count();
  if (!std::isnan(params_ptr_->time_limit) &&
      timediff_sec >= params_ptr_->time_limit) {
    return true;
  }
  // Check input label
  return checkValidLabel(direction, label);
}

void BiDirectional::updateCurrentLabel(const Directions& direction) {
  Search* search_ptr = getSearchPtr(direction);
  if (search_ptr->unprocessed_labels->size() > 0) {
    // Get next label and removes current_label from heap
    const labelling::Label& new_label = labelling::getNextLabel(
        search_ptr->unprocessed_labels.get(), direction);
    // swap current label with new label
    search_ptr->replaceCurrentLabel(new_label);
    // Update unprocessed label counter
    search_ptr->unprocessed_count = search_ptr->unprocessed_labels->size();
  } else {
    search_ptr->stop = true;
  }
}

/* Checks */
bool BiDirectional::checkValidLabel(
    const Directions&       direction,
    const labelling::Label& label) {
  if (label.vertex.lemon_id != -1 &&
      label.checkStPath(graph_ptr_->source.user_id, graph_ptr_->sink.user_id)) {
    if (!std::isnan(params_ptr_->threshold) &&
        label.checkThreshold(params_ptr_->threshold)) {
      terminated_early_w_st_path_           = true;
      terminated_early_w_st_path_direction_ = direction;
      return true;
    }
  }
  return false;
}

bool BiDirectional::checkBounds(const Directions& direction) {
  // Check resource bounds
  Search*    search_ptr = getSearchPtr(direction);
  const int& c_res      = params_ptr_->critical_res;

  if ((direction == FWD &&
       search_ptr->current_label->resource_consumption[c_res] <=
           max_res_curr_[c_res]) ||
      (direction == BWD &&
       search_ptr->current_label->resource_consumption[c_res] >
           min_res_curr_[c_res]) ||
      max_res_curr_[c_res] != min_res_curr_[c_res]) {
    return false;
  }
  // only stop if search is being performed in both directions
  else if (params_ptr_->direction == BOTH) {
    return true;
  }
  return false;
}

bool BiDirectional::checkPrimalBound(
    const Directions&       direction,
    const labelling::Label& candidate_label) {
  Search* search_ptr = getSearchPtr(direction);
  const std::unique_ptr<std::vector<double>>& lower_bound_weight =
      search_ptr->lower_bound_weight;
  if (!params_ptr_->bounds_pruning) {
    return false;
  }
  if (!std::isnan(primal_st_bound_) &&
      candidate_label.weight +
              (*lower_bound_weight)[candidate_label.vertex.lemon_id] >
          primal_st_bound_) {
    return true;
  }
  return false;
}

bool BiDirectional::checkVertexVisited(
    const Directions& direction,
    const int&        vertex_idx) {
  Search* search_ptr = getSearchPtr(direction);
  return (
      search_ptr->visited_vertices.find(vertex_idx) !=
      search_ptr->visited_vertices.end());
}

void BiDirectional::updateHalfWayPoints(const Directions& direction) {
  Search*    search_ptr = getSearchPtr(direction);
  const int& c_res      = params_ptr_->critical_res;
  if (direction == FWD) {
    min_res_curr_[c_res] = std::max(
        min_res_curr_[c_res],
        std::min(
            search_ptr->current_label->resource_consumption[c_res],
            max_res_curr_[c_res]));
  } else {
    max_res_curr_[c_res] = std::min(
        max_res_curr_[c_res],
        std::max(
            search_ptr->current_label->resource_consumption[c_res],
            min_res_curr_[c_res]));
  }
}

void BiDirectional::extendCurrentLabel(const Directions& direction) {
  // Extend and check current resource feasibility for each edge
  Search*                            search_ptr    = getSearchPtr(direction);
  std::shared_ptr<labelling::Label>& current_label = search_ptr->current_label;
  if (direction == FWD) {
    // For each outgoing arc from the current label
    for (LemonGraph::OutArcIt a(
             *graph_ptr_->lemon_graph_ptr,
             graph_ptr_->getLNodeFromId(current_label->vertex.lemon_id));
         a != lemon::INVALID;
         ++a) {
      extendSingleLabel(
          current_label.get(), direction, graph_ptr_->getAdjVertex(a, true));
    }
  } else {
    // For each incoming arc to the current label
    for (LemonGraph::InArcIt a(
             *graph_ptr_->lemon_graph_ptr,
             graph_ptr_->getLNodeFromId(current_label->vertex.lemon_id));
         a != lemon::INVALID;
         ++a) {
      extendSingleLabel(
          current_label.get(), direction, graph_ptr_->getAdjVertex(a, false));
    }
  }
}

void BiDirectional::extendSingleLabel(
    labelling::Label* label,
    const Directions& direction,
    const AdjVertex&  adj_vertex) {
  if ((params_ptr_->elementary &&
       std::find(
           label->unreachable_nodes.begin(),
           label->unreachable_nodes.end(),
           adj_vertex.vertex.user_id) == label->unreachable_nodes.end()) ||
      !params_ptr_->elementary) {
    // extend current label along edge
    labelling::Label new_label =
        label->extend(adj_vertex, direction, max_res_curr_, min_res_curr_);
    // If label non-empty, (only when the extension is resource-feasible)
    if (new_label.vertex.lemon_id != -1) {
      updateEfficientLabels(direction, new_label);
    }
  }
}

void BiDirectional::updateEfficientLabels(
    const Directions&       direction,
    const labelling::Label& candidate_label) {
  Search* search_ptr = getSearchPtr(direction);
  // const ref vertex index
  const int& lemon_id = candidate_label.vertex.lemon_id;
  // ref efficient_labels_ for a given vertex
  std::vector<labelling::Label>& efficient_labels_vertex =
      search_ptr->efficient_labels[lemon_id];

  if (candidate_label.vertex.lemon_id != -1) {
    if (std::find(
            efficient_labels_vertex.begin(),
            efficient_labels_vertex.end(),
            candidate_label) == efficient_labels_vertex.end()) {
      ++search_ptr->generated_count;
      // If there already exists labels for the given vertex
      if (efficient_labels_vertex.size() > 1) {
        // check if new_label is dominated by any other comparable label
        const bool dominated = runDominanceEff(
            &efficient_labels_vertex,
            candidate_label,
            direction,
            params_ptr_->elementary);
        if (!dominated && !checkPrimalBound(direction, candidate_label)) {
          // add candidate_label to efficient_labels and unprocessed heap
          search_ptr->pushEfficientLabel(lemon_id, candidate_label);
          search_ptr->pushUnprocessedLabel(candidate_label);
        }
      }
      // First label produced for the vertex
      else {
        // update both efficient and unprocessed labels
        search_ptr->pushEfficientLabel(lemon_id, candidate_label);
        search_ptr->pushUnprocessedLabel(candidate_label);
      }
      updateBestLabels(direction, candidate_label);
      // Update vertices visited
      search_ptr->addVisitedVertex(lemon_id);
    }
  }
}

void BiDirectional::updateBestLabels(
    const Directions&       direction,
    const labelling::Label& candidate_label) {
  // Only save full paths when they are global resource feasible
  Search*    search_ptr = getSearchPtr(direction);
  const int& lemon_id   = candidate_label.vertex.lemon_id;
  std::vector<std::shared_ptr<labelling::Label>>& best_labels =
      search_ptr->best_labels;

  if (direction == FWD && lemon_id == graph_ptr_->sink.lemon_id &&
      !candidate_label.checkFeasibility(max_res, min_res)) {
    return;
  } else if (
      direction == BWD && lemon_id == graph_ptr_->source.lemon_id &&
      !candidate_label.checkFeasibility(max_res, min_res)) {
    return;
  }
  // Update best_label only when new label has lower weight or first label
  if ((best_labels[lemon_id] &&
       candidate_label.weight < best_labels[lemon_id]->weight) ||
      !best_labels[lemon_id]) {
    search_ptr->replaceBestLabel(lemon_id, candidate_label);
  }
}

void BiDirectional::saveCurrentBestLabel(const Directions& direction) {
  Search* search_ptr = getSearchPtr(direction);

  std::shared_ptr<labelling::Label>& intermediate_label_ptr =
      search_ptr->intermediate_label;
  std::shared_ptr<labelling::Label>& current_label_ptr =
      search_ptr->current_label;

  if (intermediate_label_ptr->vertex.lemon_id == -1) {
    intermediate_label_ptr =
        std::make_shared<labelling::Label>(*current_label_ptr);
    return;
  }
  // Check for global feasibility
  if (!current_label_ptr->checkFeasibility(max_res, min_res)) {
    return;
  }
  if (intermediate_label_ptr->vertex.lemon_id ==
          current_label_ptr->vertex.lemon_id &&
      current_label_ptr->fullDominance(*intermediate_label_ptr, direction)) {
    // Save complete source-sink path
    search_ptr->replaceIntermediateLabel(*current_label_ptr);
  } else {
    // First source-sink path
    if ((direction == FWD &&
         (current_label_ptr->partial_path.back() == graph_ptr_->sink.user_id &&
          intermediate_label_ptr->vertex.user_id ==
              graph_ptr_->source.user_id)) ||
        (direction == BWD && (current_label_ptr->partial_path.back() ==
                                  graph_ptr_->source.user_id &&
                              intermediate_label_ptr->vertex.user_id ==
                                  graph_ptr_->sink.user_id))) {
      // Save complete source-sink path
      search_ptr->replaceIntermediateLabel(*current_label_ptr);
      // Update bounds
      if (std::isnan(primal_st_bound_) ||
          intermediate_label_ptr->weight < primal_st_bound_) {
        primal_st_bound_ = intermediate_label_ptr->weight;
      }
    }
  }
}

/**
 * Post-processing methods
 */

void BiDirectional::postProcessing() {
  if (!terminated_early_w_st_path_) {
    if (params_ptr_->direction == BOTH) {
      // If bidirectional algorithm used and both directions traversed, run
      // path joining procedure.
      joinLabels();
    } else {
      // If FWD direction specified or backward direction not traversed
      if (params_ptr_->direction == FWD) {
        // Forward
        best_label_ = fwd_search_ptr_->intermediate_label;
      }
      // If backward direction specified or FWD direction not traversed
      else {
        // Backward
        best_label_ =
            std::make_shared<labelling::Label>(labelling::processBwdLabel(
                *bwd_search_ptr_->intermediate_label, max_res, min_res, true));
      }
    }
  } else {
    // final label contains the label that triggered the early termination
    if (terminated_early_w_st_path_direction_ == FWD) {
      best_label_ = fwd_search_ptr_->intermediate_label;
    } else {
      best_label_ =
          std::make_shared<labelling::Label>(labelling::processBwdLabel(
              *bwd_search_ptr_->intermediate_label, max_res, min_res, true));
    }
  }
}

double BiDirectional::getUB() {
  double UB = INF;
  // Extract forward and backward best labels (one's with least weight)
  const auto& fwd_best =
      fwd_search_ptr_->best_labels[graph_ptr_->sink.lemon_id];
  const auto& bwd_best =
      bwd_search_ptr_->best_labels[graph_ptr_->source.lemon_id];
  // Upper bound must be a resource-feasible s-t path
  if (fwd_best && fwd_best->checkFeasibility(max_res, min_res)) {
    UB = fwd_best->weight;
  }
  if (bwd_best && bwd_best->checkFeasibility(max_res, min_res)) {
    if (bwd_best->weight < UB) {
      UB = bwd_best->weight;
    }
  }
  return UB;
}

void BiDirectional::getMinimumWeights(double* fwd_min, double* bwd_min) {
  // Forward
  // init
  *fwd_min = INF;
  for (const int& n : fwd_search_ptr_->visited_vertices) {
    if (n != graph_ptr_->source.lemon_id && fwd_search_ptr_->best_labels[n] &&
        fwd_search_ptr_->best_labels[n]->weight < *fwd_min) {
      *fwd_min = fwd_search_ptr_->best_labels[n]->weight;
    }
  }
  // backward
  *bwd_min = INF;
  for (const int& n : bwd_search_ptr_->visited_vertices) {
    if (n != graph_ptr_->sink.lemon_id && bwd_search_ptr_->best_labels[n] &&
        bwd_search_ptr_->best_labels[n]->weight < *bwd_min) {
      *bwd_min = bwd_search_ptr_->best_labels[n]->weight;
    }
  }
}

void BiDirectional::joinLabels() {
  // ref id with critical_res
  const int&    c_res   = params_ptr_->critical_res;
  double        UB      = getUB();
  const double& HF      = std::min(max_res_curr_[c_res], min_res_curr_[c_res]);
  auto          fwd_min = std::make_unique<double>();
  auto          bwd_min = std::make_unique<double>();
  // lower bounds on forward and backward labels
  getMinimumWeights(fwd_min.get(), bwd_min.get());

  std::vector<labelling::Label> merged_labels_;

  // for each vertex visited forward
  for (const int& n : fwd_search_ptr_->visited_vertices) {
    // if bound check fwd_label
    if (fwd_search_ptr_->best_labels[n]->weight + *bwd_min <= UB &&
        n != graph_ptr_->sink.lemon_id) {
      // for each forward label at n
      for (auto fwd_iter = fwd_search_ptr_->efficient_labels[n].cbegin();
           fwd_iter != fwd_search_ptr_->efficient_labels[n].cend();
           ++fwd_iter) {
        const labelling::Label& fwd_label = *fwd_iter;
        // if bound check fwd_label
        if (fwd_label.resource_consumption[c_res] <= HF &&
            fwd_label.weight + *bwd_min <= UB) {
          // for each successor of n
          for (LemonGraph::OutArcIt a(
                   *graph_ptr_->lemon_graph_ptr, graph_ptr_->getLNodeFromId(n));
               a != lemon::INVALID;
               ++a) {
            const int&    m           = graph_ptr_->getId(graph_ptr_->head(a));
            const double& edge_weight = graph_ptr_->getWeight(a);
            if (checkVertexVisited(BWD, m) &&
                m != graph_ptr_->source.lemon_id &&
                (fwd_label.weight + edge_weight +
                     bwd_search_ptr_->best_labels[m]->weight <=
                 UB)) {
              // for each backward label at m
              for (auto bwd_iter =
                       bwd_search_ptr_->efficient_labels[m].cbegin();
                   bwd_iter != bwd_search_ptr_->efficient_labels[m].cend();
                   ++bwd_iter) {
                const labelling::Label& bwd_label = *bwd_iter;
                // TODO: should suffice with strict > HF, but Beasley 10
                // fails
                if (bwd_label.resource_consumption[c_res] >= HF &&
                    (fwd_label.weight + edge_weight + bwd_label.weight <= UB) &&
                    labelling::mergePreCheck(fwd_label, bwd_label, max_res)) {
                  const labelling::Label& merged_label = labelling::mergeLabels(
                      fwd_label,
                      bwd_label,
                      graph_ptr_->getAdjVertex(a, true),
                      graph_ptr_->sink,
                      max_res,
                      min_res);
                  if (merged_label.vertex.lemon_id != -1 &&
                      merged_label.checkFeasibility(max_res, min_res) &&
                      labelling::halfwayCheck(merged_label, merged_labels_)) {
                    if (best_label_->vertex.lemon_id == -1 ||
                        (merged_label.fullDominance(*best_label_, FWD) ||
                         merged_label.weight < best_label_->weight)) {
                      // Save
                      best_label_ =
                          std::make_shared<labelling::Label>(merged_label);
                      // Tighten UB
                      if (best_label_->weight < UB) {
                        UB = best_label_->weight;
                      }
                      // Stop if time out or threshold found
                      if (terminate(FWD, *best_label_)) {
                        return;
                      }
                    }
                  }
                  // Add merged label to list
                  merged_labels_.push_back(merged_label);
                }
              }
            }
          }
        }
      }
    }
  }
} // end joinLabels

} // namespace bidirectional
