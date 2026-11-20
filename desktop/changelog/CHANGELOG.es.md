# Cambios

<!-- Spanish translation of CHANGELOG.md, shown in the app ("What's new").
     Only the newest versions are translated; older ones show in English. -->

## Unreleased

- Nuevos idiomas de la interfaz: chino simplificado (简体中文, aportado por la comunidad), francés, italiano, español y ruso. Se eligen en el menú Idioma. Francés, italiano, español y ruso se tradujeron con una herramienta de IA y aún no los han revisado hablantes nativos; las correcciones son bienvenidas como incidencia Translation.
- Los textos que aún no están traducidos aparecen en inglés.
- Traducir la aplicación a más idiomas ahora es mucho más fácil: consulta TRANSLATING.md o abre una incidencia Translation.
- Los términos del juego (atributos, demarcaciones, pierna, personalidades, minutos acordados y más) siguen ahora los textos oficiales de cada versión idiomática del juego.
- Los nombres y abreviaturas de los roles de FM27 siguen el idioma de la interfaz, tal como en esa versión idiomática del juego. Tus tácticas y ajustes no cambian.
- Los valores de los jugadores siguen igualmente el idioma de la interfaz: atributos, personalidad, pierna, pierna buena y demarcaciones.
- El dinero puede mostrarse en libras, dólares estadounidenses, euros, francos suizos, yuanes chinos o rublos rusos (Idioma → Moneda), convertido con los tipos de cambio del propio juego. Por defecto la moneda corresponde al idioma de la interfaz. Los valores guardados no cambian.
- Para FM27 la importación ofrece ahora primero la partida. Las exportaciones HTML/CSV siguen funcionando como antes.
- Las exportaciones HTML/CSV se leen en todos los idiomas de juego admitidos (inglés, alemán, francés, italiano, chino simplificado, ruso, español). El idioma se reconoce por los encabezados de columna; también se puede fijar en Ajustes → Importación.
- Comprobación de actualizaciones opcional, desactivada por defecto: la aplicación pregunta una vez si puede buscar nuevas versiones al iniciarse. Solo pregunta a GitHub por la última versión y no envía datos sobre ti ni sobre tus partidas.
- Una nueva versión aparece como aviso con sus cambios y un botón de descarga que abre la página de la versión. No se instala nada automáticamente; una versión se puede omitir.
- Ajustes → Partidas → Actualizaciones: activa o desactiva la comprobación, incluye versiones de prueba o comprueba ahora.
- Tras una actualización, la aplicación muestra una vez las novedades, en el idioma de la interfaz. «No volver a mostrar» lo desactiva (también en Ajustes → Partidas → Actualizaciones); Ayuda → Novedades las muestra en cualquier momento. El aviso de actualización también muestra los cambios de la nueva versión en el idioma de la interfaz.
- Mejor XI de FM27: un jugador puede cubrir un puesto fuera de sus demarcaciones en una de las dos alineaciones, para que los puestos ya no queden vacíos solo porque nadie tiene ambas demarcaciones. Su valor allí se reduce (factor 0,80, Ajustes → Plantilla y análisis); el rol aparece rayado.
- Los jugadores también se valoran para roles que no tienen asignados cuando la alineación los necesita.
- Vista combinada: un puesto libre con y sin posesión aparece como un solo recuadro, y los puestos libres ya no dibujan líneas de unión.
