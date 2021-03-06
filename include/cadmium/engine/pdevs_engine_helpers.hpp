/**
 * Copyright (c) 2013-2016, Damian Vicino
 * Carleton University, Universite de Nice-Sophia Antipolis
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef PDEVS_ENGINE_HELPERS_HPP
#define PDEVS_ENGINE_HELPERS_HPP

#include <type_traits>
#include <tuple>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <boost/type_index.hpp>
#include <cadmium/concept/concept_helpers.hpp>
#include <cadmium/modeling/message_bag.hpp>
#include <cadmium/logger/common_loggers.hpp>


namespace cadmium {
    namespace engine {
        //forward declaration
        template<template<typename T> class MODEL, typename TIME, typename LOGGER>
        class coordinator;        //forward declaration
        template<template<typename T> class MODEL, typename TIME, typename LOGGER>
        class simulator;

        // Displaying all messages in a bag
        //printing all messages in bags, if the support the << operator to ostream
        template <typename T>
        struct is_streamable {
        private:
            template <typename U>
            static decltype(std::cout << std::declval<U>(), void(), std::true_type()) test(int);
            template <typename>
            static std::false_type test(...);
        public:
            using type=decltype(test<T>(0));
            static constexpr auto value=type::value;
        };

        template<typename T>
        constexpr bool is_streamable_v(){
            return is_streamable<T>::value;
        }

        template<typename T, typename V=typename is_streamable<T>::type>
        struct value_or_name;

        template<typename T>
        struct value_or_name<T, std::true_type>{
            static void print(std::ostream& os, const T& v){
                os << v;
            }
        };

        template<typename T>
        struct value_or_name<T, std::false_type>{
            static void print(std::ostream& os, const T& v){
                os << "obscure message of type ";
                os << boost::typeindex::type_id<T>().pretty_name();
            }
        };


        template<typename T>
        std::ostream& implode(std::ostream& os, const T& collection){
             os << "{";
             auto it = std::begin(collection);
             if (it != std::end(collection)) {
                value_or_name<typename T::value_type>::print(os, *it);
                ++it;
             }
             while (it != std::end(collection)){
                os << ", ";
                value_or_name<typename T::value_type>::print(os, *it);
                ++it;
             }
             os << "}";
             return os;
        }

        //finding the min next from a tuple of coordinators and simulators
        template<typename T, std::size_t S>
        struct min_next_in_tuple_impl {
            static auto value(T& t) {
                return std::min(std::get<S - 1>(t).next(), min_next_in_tuple_impl<T, S - 1>::value(t));
            }
        };

        template<typename T>
        struct min_next_in_tuple_impl<T, 1> {
            static auto value(T& t) {
                return std::get<0>(t).next();
            }
        };

        template<typename T>
        auto min_next_in_tuple(T& t) {
            return min_next_in_tuple_impl<T, std::tuple_size<T>::value>::value(t);
        }

        //We use COS to accumulate coordinators and simulators while iterating MT using the S index
        template<typename TIME, template<typename> class MT, std::size_t S, typename LOGGER, typename... COS> //COS accumulates coords or sims
        struct coordinate_tuple_impl {
            template<typename T>
            using current=typename std::tuple_element<S - 1, MT<T>>::type;
            using current_coordinated=typename std::conditional<cadmium::concept::is_atomic<current>::value(), simulator<current, TIME, LOGGER>, coordinator<current, TIME, LOGGER>>::type;
            using type=typename coordinate_tuple_impl<TIME, MT, S - 1, LOGGER, current_coordinated, COS...>::type;
        };

        //When the S reaches 0, all coordinators and simulators are put into a tuple for return
        template<typename TIME, template<typename> class MT, typename LOGGER, typename... COS>
        struct coordinate_tuple_impl<TIME, MT, 0, LOGGER, COS...> {
            using type=std::tuple<COS...>;
        };

        template<typename TIME, template<typename> class MT, typename LOGGER>
        struct coordinate_tuple {
            //the size should not be affected by the type used for TIME, simplifying passing float
            using type=typename coordinate_tuple_impl<TIME, MT, std::tuple_size<MT<float>>::value, LOGGER>::type;
        };

        //init every subcooridnator in the coordination tuple
        template <typename TIME, typename  CST, std::size_t S>
        struct init_subcoordinators_impl{
            static void value(const TIME& t, CST& cs){
                std::get<S-1>(cs).init(t);
                init_subcoordinators_impl<TIME, CST, S-1>::value(t, cs);
                return;
            }
        };

        template <typename TIME, typename  CST>
        struct init_subcoordinators_impl<TIME, CST, 1>{
            static void value(const TIME& t, CST& cs){
                std::get<0>(cs).init(t);
                return;
            }
        };

        template<typename TIME, typename CST>
        void init_subcoordinators(const TIME& t, CST& cs ) {
            init_subcoordinators_impl<TIME, CST, std::tuple_size<CST>::value>::value(t, cs);
        }

        //populate the outbox of every subcoordinator recursively
        template<typename TIME, typename CST, std::size_t S>
        struct collect_outputs_in_subcoordinators_impl{
            static void run(const TIME& t, CST& cs){
                std::get<S-1>(cs).collect_outputs(t);
                collect_outputs_in_subcoordinators_impl<TIME, CST, S-1>::run(t, cs);
            }
        };

        template<typename TIME, typename CST>
        struct collect_outputs_in_subcoordinators_impl<TIME, CST, 0>{
            static void run(const TIME& t, CST& cs){}
        };

        template<typename TIME, typename CST>
        void collect_outputs_in_subcoordinators(const TIME& t, CST& cs){
            collect_outputs_in_subcoordinators_impl<TIME, CST, std::tuple_size<CST>::value>::run(t, cs);
        }

        //get the engine  from a tuple of engines that is simulating the model provided
        template<typename TIMED_MODEL, typename CST, size_t S>
        struct get_engine_by_model_impl{
            using current_engine=typename std::tuple_element<S-1, CST>::type;
            using current_model=typename current_engine::model_type;
            using type=typename std::conditional<std::is_same<current_model, TIMED_MODEL>::value, current_engine, typename get_engine_by_model_impl<TIMED_MODEL, CST, S-1>::type>::type;
        };

        struct NO_SIMULATOR{};

        template<typename TIMED_MODEL, typename CST>
        struct get_engine_by_model_impl<TIMED_MODEL, CST, 1>{
            using current_engine=typename std::tuple_element<0, CST>::type;
            using current_model=typename current_engine::model_type;
            using type=typename std::conditional<std::is_same<current_model, TIMED_MODEL>::value, current_engine, NO_SIMULATOR>::type;
        };

        template<typename TIMED_MODEL, typename CST>
        struct get_engine_type_by_model{
            using type=typename get_engine_by_model_impl<TIMED_MODEL, CST, std::tuple_size<CST>::value>::type;
        };

        template<typename TIMED_MODEL, typename CST>
        //typename get_engine_type_by_model<TIMED_MODEL, CST>::type
        typename get_engine_type_by_model<TIMED_MODEL, CST>::type & get_engine_by_model(CST& cst){
            return std::get<typename get_engine_type_by_model<TIMED_MODEL, CST>::type>(cst);
        }

        //map the messages in the outboxes of subengines to the messages in the outbox of current coordinator
        template<typename TIME, typename EOC, std::size_t S, typename OUT_BAG, typename CST, typename LOGGER>
        struct collect_messages_by_eoc_impl{
            using external_output_port=typename std::tuple_element<S-1, EOC>::type::external_output_port;
            using submodel_from = typename std::tuple_element<S-1, EOC>::type::template submodel<TIME>;
            using submodel_output_port=typename std::tuple_element<S-1, EOC>::type::submodel_output_port;
            using submodel_out_messages_type=typename make_message_bags<typename std::tuple<submodel_output_port>>::type;

            static void fill(OUT_BAG& messages, CST& cst){
                //process one coupling
                auto from_bag = get_engine_by_model<submodel_from, CST>(cst).outbox();
                auto& from_messages = get_messages<submodel_output_port>(from_bag);
                auto& to_messages = get_messages<external_output_port>(messages);
                to_messages.insert(to_messages.end(), from_messages.begin(), from_messages.end());
                //log
                auto log_routing_collect = [](decltype(from_messages) from, decltype(to_messages) to) -> std::string {
                     std::ostringstream oss;
                     oss << " in port ";
                     oss << boost::typeindex::type_id<external_output_port>().pretty_name();
                     oss << " has ";
                     implode(oss, to);
                     oss << " routed from ";
                     oss << boost::typeindex::type_id<submodel_output_port>().pretty_name();
                     oss << " of model ";
                     oss << boost::typeindex::type_id<submodel_from>().pretty_name();
                     oss << " with messages ";
                     implode(oss, from);
                     return oss.str();
                };
                LOGGER::template log<cadmium::logger::logger_message_routing,
                                     decltype(log_routing_collect),
                                     decltype(from_messages),
                                     decltype(to_messages)>(log_routing_collect, from_messages, to_messages);

                //iterate
                collect_messages_by_eoc_impl<TIME, EOC, S-1, OUT_BAG, CST, LOGGER>::fill(messages, cst);
            }
        };

        template<typename TIME, typename EOC, typename OUT_BAG, typename CST, typename LOGGER>
        struct collect_messages_by_eoc_impl<TIME, EOC, 0, OUT_BAG, CST, LOGGER>{
            static void fill(OUT_BAG& messages, CST& cst){} //nothing to do here
        };

        template<typename TIME, typename EOC, typename OUT_BAG, typename CST,typename LOGGER>
        OUT_BAG collect_messages_by_eoc(CST& cst){
            OUT_BAG ret;//if the subcoordinators active are not connected by EOC, no output is generated
            collect_messages_by_eoc_impl<TIME, EOC, std::tuple_size<EOC>::value, OUT_BAG, CST, LOGGER>::fill(ret, cst);
            return ret;
        }

        //advance the simulation in every subengine
        template <typename TIME, typename CST, std::size_t S>
        struct advance_simulation_in_subengines_impl{
            static void run(const TIME& t, CST& cst){
                std::get<S-1>(cst).advance_simulation(t);
                advance_simulation_in_subengines_impl<TIME, CST, S-1>::run(t, cst);
            }
        };

        template <typename TIME, typename CST>
        struct advance_simulation_in_subengines_impl<TIME, CST, 0>{
            static void run(const TIME& t, CST& cst){
                //nothing to do here
            }
        };

        template <typename TIME, typename CST>
        void advance_simulation_in_subengines(const TIME& t, CST& subcoordinators){
            advance_simulation_in_subengines_impl<TIME, CST, std::tuple_size<CST>::value>::run(t, subcoordinators);
            return;
        }


        //route messages following ICs
        template<typename TIME, typename CST, typename ICs, std::size_t S, typename LOGGER>
        struct route_internal_coupled_messages_on_subcoordinators_impl{
            using current_IC=typename std::tuple_element<S-1, ICs>::type;
            using from_model=typename current_IC::template from_model<TIME>;
            using from_port=typename current_IC::from_model_output_port;
            using to_model=typename current_IC::template to_model<TIME>;
            using to_port=typename current_IC::to_model_input_port;

            using from_model_type=typename get_engine_type_by_model<from_model, CST>::type;
            using to_model_type=typename get_engine_type_by_model<to_model, CST>::type;
            static void route(const TIME& t, CST& engines){
                //route messages for 1 coupling
                from_model_type& from_engine = get_engine_by_model<from_model, CST>(engines);
                to_model_type& to_engine=get_engine_by_model<to_model, CST>(engines);

                //add the messages
                auto& from_messages = cadmium::get_messages<from_port>(from_engine._outbox);
                auto& to_messages = cadmium::get_messages<to_port>(to_engine._inbox);
                to_messages.insert(to_messages.end(), from_messages.begin(), from_messages.end());

                //log
                auto log_routing_collect = [](decltype(from_messages) from, decltype(to_messages) to) -> std::string {
                     std::ostringstream oss;
                     oss << " in port ";
                     oss << boost::typeindex::type_id<to_port>().pretty_name();
                     oss << " of model ";
                     oss << boost::typeindex::type_id<to_model>().pretty_name();
                     oss << " has ";
                     implode(oss, to);
                     oss << " routed from ";
                     oss << boost::typeindex::type_id<from_port>().pretty_name();
                     oss << " of model ";
                     oss << boost::typeindex::type_id<from_model>().pretty_name();
                     oss << " with messages ";
                     implode(oss, from);
                     return oss.str();
                };
                LOGGER::template log<cadmium::logger::logger_message_routing,
                                     decltype(log_routing_collect),
                                     decltype(from_messages),
                                     decltype(to_messages)>(log_routing_collect, from_messages, to_messages);

                //iterate
                route_internal_coupled_messages_on_subcoordinators_impl<TIME, CST, ICs, S-1, LOGGER>::route(t, engines);
            }
        };

        template<typename TIME, typename CST, typename ICs, typename LOGGER>
        struct route_internal_coupled_messages_on_subcoordinators_impl<TIME, CST, ICs, 0, LOGGER>{
            static void route(const TIME& t, CST& subcoordinators){
            //nothing to do here
            }
        };

        template <typename TIME, typename CST, typename ICs, typename LOGGER >
        void route_internal_coupled_messages_on_subcoordinators(const TIME& t, CST& cst){
            route_internal_coupled_messages_on_subcoordinators_impl<TIME, CST, ICs, std::tuple_size<ICs>::value, LOGGER>::route(t, cst);
            return;
        };

        template<typename TIME, typename INBAGS, typename CST, typename EICs, size_t S, typename LOGGER>
        struct route_external_input_coupled_messages_on_subcoordinators_impl{
            using current_EIC=typename std::tuple_element<S-1, EICs>::type;
            using from_port=typename current_EIC::from_port;
            using to_model=typename current_EIC::template to_model<TIME>;
            using to_port=typename current_EIC::to_model_input_port;

            static void route(TIME t, const INBAGS& inbox, CST& engines){
                auto to_engine=get_engine_by_model<to_model, CST>(engines);
                auto& from_messages = cadmium::get_messages<from_port>(inbox);
                auto& to_messages = cadmium::get_messages<to_port>(to_engine._inbox);
                to_messages.insert(to_messages.end(), from_messages.begin(), from_messages.end());

                //log
                auto log_routing_collect = [](decltype(from_messages) from, decltype(to_messages) to) -> std::string {
                     std::ostringstream oss;
                     oss << " in port ";
                     oss << boost::typeindex::type_id<to_port>().pretty_name();
                     oss << " of model ";
                     oss << boost::typeindex::type_id<to_model>().pretty_name();
                     oss << " has ";
                     implode(oss, to);
                     oss << " routed from ";
                     oss << boost::typeindex::type_id<from_port>().pretty_name();
                     oss << " with messages ";
                     implode(oss, from);
                     return oss.str();
                };
                LOGGER::template log<cadmium::logger::logger_message_routing,
                                     decltype(log_routing_collect),
                                     decltype(from_messages),
                                     decltype(to_messages)>(log_routing_collect, from_messages, to_messages);

                //iterate
                route_external_input_coupled_messages_on_subcoordinators_impl<TIME, INBAGS, CST, EICs, std::tuple_size<EICs>::value, LOGGER>::route(t, inbox, engines);

            }
        };

        template<typename TIME, typename INBAGS, typename CST, typename EICs, typename LOGGER>
        struct route_external_input_coupled_messages_on_subcoordinators_impl<TIME, INBAGS, CST, EICs, 0, LOGGER>{
            static void route(TIME T, const INBAGS& inbox, CST& engines){
            //nothing to do here
            }
        };


        template <typename TIME, typename INBAGS, typename CST, typename EICs, typename LOGGER >
        void route_external_input_coupled_messages_on_subcoordinators(const TIME& t, const INBAGS& inbox, CST& cst){
                route_external_input_coupled_messages_on_subcoordinators_impl<TIME, INBAGS, CST, EICs, std::tuple_size<EICs>::value, LOGGER>::route(t, inbox, cst);
            return;
        };

        //auxiliary
        template<size_t I, typename... Ps>
        struct all_bags_empty_impl {
            static bool check(std::tuple<Ps...> t) {
                if (!std::get<I - 1>(t).messages.empty()) return false;
                return all_bags_empty_impl<I - 1, Ps...>::check(t);
            }
        };

        template<typename... Ps>
        struct all_bags_empty_impl<0, Ps...> {
            static bool check(std::tuple<Ps...> t) {
                return true;
            }
        };

        template<typename... Ps>
        bool all_bags_empty(std::tuple<Ps...> t) {
            return (std::tuple_size < std::tuple < Ps...>>() == 0 )
            || all_bags_empty_impl<std::tuple_size<decltype(t)>::value, Ps...>::check(t);
        }


        //priting messages
        template<size_t s, typename... T>
        struct print_messages_by_port_impl{
            using current_bag=typename std::tuple_element<s-1, std::tuple<T...>>::type;
            static void run(std::ostream& os, const std::tuple<T...>& b){
                print_messages_by_port_impl<s-1, T...>::run(os, b);
                os << ", ";
                os << boost::typeindex::type_id<typename current_bag::port>().pretty_name();
                os << ": ";
                implode(os, cadmium::get_messages<typename current_bag::port>(b));
            }
        };

        template<typename... T>
        struct print_messages_by_port_impl<1, T...>{
            using current_bag=typename std::tuple_element<0, std::tuple<T...>>::type;
            static void run(std::ostream& os, const std::tuple<T...>& b){
                os << boost::typeindex::type_id<typename current_bag::port>().pretty_name();
                os << ": ";
                implode(os, cadmium::get_messages<typename current_bag::port>(b));
            }
        };

        template<typename... T>
        struct print_messages_by_port_impl<0, T...>{
            static void run(std::ostream& os, const std::tuple<T...>& b){}
        };

        template <typename... T>
        void print_messages_by_port(std::ostream& os, const std::tuple<T...>& b){
               os << "[";
               print_messages_by_port_impl<sizeof...(T), T...>::run(os, b);
               os << "]";
        }
    }


}
#endif // PDEVS_ENGINE_HELPERS_HPP
