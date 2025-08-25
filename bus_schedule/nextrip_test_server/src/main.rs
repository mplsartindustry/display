use std::net::SocketAddr;

use axum::{Json, Router, extract::Path, routing::get};
use axum_server::tls_rustls::RustlsConfig;
use serde::Serialize;
use tracing::Level;

#[derive(Serialize)]
enum ScheduleRelationship {
    NoData,
    Scheduled,
    Skipped,
}

#[derive(Serialize)]
struct Departure {
    actual: bool,
    trip_id: String,
    departure_text: String,
    route_short_name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    terminal: Option<String>,
    schedule_relationship: ScheduleRelationship,
}

#[derive(Serialize)]
struct NexTripResponse {
    departures: Vec<Departure>,
}

async fn get_hello() -> &'static str {
    "Hello!"
}

async fn get_nextrip(Path(stop_id): Path<i32>) -> Json<NexTripResponse> {
    tracing::info!("Request for stop ID: {stop_id}");

    Json(NexTripResponse {
        departures: vec![
            Departure {
                actual: true,
                trip_id: "foo".into(),
                departure_text: "Due".into(),
                route_short_name: "0".into(),
                terminal: Some("Z".into()),
                schedule_relationship: ScheduleRelationship::Scheduled,
            },
            Departure {
                actual: true,
                trip_id: "bar".into(),
                departure_text: "4 Min".into(),
                route_short_name: "012".into(),
                terminal: None,
                schedule_relationship: ScheduleRelationship::Scheduled,
            },
            Departure {
                actual: false,
                trip_id: "baz".into(),
                departure_text: "12:34".into(),
                route_short_name: "99".into(),
                terminal: Some("E".into()),
                schedule_relationship: ScheduleRelationship::Skipped,
            },
            Departure {
                actual: false,
                trip_id: "qux".into(),
                departure_text: "1:23".into(),
                route_short_name: "42".into(),
                terminal: Some("B".into()),
                schedule_relationship: ScheduleRelationship::NoData,
            },
        ],
    })
}

#[tokio::main]
async fn main() {
    // Set up logging
    tracing_subscriber::fmt()
        .with_max_level(Level::TRACE)
        .init();

    let config = RustlsConfig::from_pem(
        include_bytes!("self_signed_cert/cert.pem").into(),
        include_bytes!("self_signed_cert/key.pem").into(),
    )
    .await
    .unwrap();

    let app = Router::new()
        .route("/hello", get(get_hello))
        .route("/nextrip/{stop_id}", get(get_nextrip));

    let addr = SocketAddr::from(([0, 0, 0, 0], 3000));
    tracing::info!("Listening on {addr}");
    axum_server::bind_rustls(addr, config)
        .serve(app.into_make_service())
        .await
        .unwrap();
}
