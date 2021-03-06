//
// request_handler.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2012 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <sstream>
#include <string>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

#include "http_server/mapnik_request_handler.hpp"
#include "http_server/parse_path.hpp"
#include "http_server/reply.hpp"
#include "http_server/request.hpp"

#include <mapnik/load_map.hpp>

// for vector tile creation
#include "avecado.hpp"
#include "tilejson.hpp"
#include "util.hpp"

namespace {
std::string make_http_date() {
  std::stringstream out;
  std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  struct tm tt;
  gmtime_r(&t, &tt);
  char *oldlocale = setlocale(LC_TIME, NULL);
  setlocale(LC_TIME, "C");
  char buf[30];
  strftime(buf, 30, "%a, %d %b %Y %H:%M:%S GMT", &tt);
  setlocale(LC_TIME, oldlocale);
  return std::string(buf);
}

std::string strip_query_params(const std::string &str) {
  return str.substr(0, str.find('?'));
}
} // anonymous namespace

namespace http {
namespace server3 {

mapnik_request_handler::mapnik_request_handler(const mapnik_server_options &options, std::string port)
  : map_(),
    options_(options),
    port_(port),
    max_age_value_((boost::format("max-age = %1%") % options_.max_age).str())
{
  std::cout << "Loading mapnik map..." << std::endl;
  mapnik::load_map(map_, options_.map_file);
  std::cout << "Mapnik map loaded." << std::endl;
}

void mapnik_request_handler::handle_request(const request& req, reply& rep)
{
  handle_request_impl(req, rep);
  if (options_.logger) { options_.logger->log(req, rep); }
}

void mapnik_request_handler::handle_request_impl(const request &req, reply &rep)
{
  // Decode url to path.
  std::string request_path;
  if (!url_decode(strip_query_params(req.uri), request_path))
  {
    rep = reply::stock_reply(reply::bad_request);
    return;
  }

  try {
    // serve tilejson
    if (request_path == "/tile.json") {
      handle_request_json(req, rep);

    } else {
      handle_request_tile(req, rep, request_path);
    }

  } catch (...) {
    rep = reply::stock_reply(reply::internal_server_error);
  }
}

void mapnik_request_handler::handle_request_json(const request &req, reply &rep) {
  std::string base_url = (boost::format("http://localhost:%1%") % port_).str();
  std::string json = avecado::make_tilejson(map_, base_url);

  rep.status = reply::ok;
  rep.is_hard_error = false;
  rep.content = std::move(json);
  rep.headers.resize(6);
  rep.headers[0].name = "Content-Length";
  rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
  rep.headers[1].name = "Content-Type";
  rep.headers[1].value = "application/json";
  rep.headers[2].name = "Access-Control-Allow-Origin";
  rep.headers[2].value = "*";
  rep.headers[3].name= "Access-Control-Allow-Methods";
  rep.headers[3].value = "GET";
  rep.headers[4].name = "Cache-control";
  rep.headers[4].value = max_age_value_;
  rep.headers[5].name = "Date";
  rep.headers[5].value = make_http_date();
}

void mapnik_request_handler::handle_request_tile(const request &req, reply &rep,
                                                 const std::string &request_path) {
  // simple hierarchy is just $z/$x/$y.pbf, in spherical mercator
  // and we don't take account of anything fancy.
  int z, x, y;
  if (!parse_path(request_path, z, x, y)) {
    rep = reply::stock_reply(reply::not_found);
    return;
  }

  // some sanity checking for z, x, y ranges
  if ((z < 0) || (z > 30)) {
    rep = reply::stock_reply(reply::not_found);
    return;
  }
  const int max_coord = 1 << z;
  if ((x < 0) || (x >= max_coord)) {
    rep = reply::stock_reply(reply::not_found);
    return;
  }
  if ((y < 0) || (y >= max_coord)) {
    rep = reply::stock_reply(reply::not_found);
    return;
  }

  // setup map parameters
  map_.resize(256, 256);
  map_.zoom_to_box(avecado::util::box_for_tile(z, x, y));

  boost::optional<const avecado::post_processor &> pp = boost::none;
  if (options_.post_processor) {
    pp = *options_.post_processor;
  }

  avecado::tile tile(z, x, y);

  // actually making the vector tile
  bool painted = avecado::make_vector_tile(
    tile, options_.path_multiplier, map_, options_.buffer_size,
    options_.scale_factor, options_.offset_x, options_.offset_y,
    options_.tolerance, options_.image_format, options_.scaling_method,
    options_.scale_denominator, pp);

  // Fill out the reply to be sent to the client.
  rep.status = reply::ok;
  rep.is_hard_error = false;
  rep.content = painted ? tile.get_data(options_.compression_level) : "";
  rep.headers.resize(7);
  rep.headers[0].name = "Content-Length";
  rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
  rep.headers[1].name = "Content-Type";
  rep.headers[1].value = "application/octet-stream";
  rep.headers[2].name = "Access-Control-Allow-Origin";
  rep.headers[2].value = "*";
  rep.headers[3].name= "Access-Control-Allow-Methods";
  rep.headers[3].value = "GET";
  rep.headers[4].name = "Cache-control";
  rep.headers[4].value = max_age_value_;
  rep.headers[5].name = "Date";
  rep.headers[5].value = make_http_date();
  // make sure that the response header is set appropriately for the level
  // of compression that we are applying, so that the client doesn't have
  // to inspect the file and try to figure out if it's supposed to be
  // compressed or not.
  rep.headers[6].name = "Content-Encoding";
  if (options_.compression_level == 0) {
    rep.headers[6].value = "identity";
  } else {
    rep.headers[6].value = "gzip";
  }
}

} // namespace server3
} // namespace http
