# Changements

<!-- French translation of CHANGELOG.md, shown in the app ("What's new").
     Only the newest versions are translated; older ones show in English. -->

## Unreleased

- Nouvelles langues de l'interface : chinois simplifié (简体中文, proposé par la communauté), français, italien, espagnol et russe. À choisir dans le menu Langue. Le français, l'italien, l'espagnol et le russe ont été traduits avec un outil d'IA et ne sont pas encore relus par des locuteurs natifs ; les corrections sont les bienvenues sous forme de ticket Translation.
- Les textes pas encore traduits s'affichent en anglais.
- Traduire l'application dans d'autres langues est désormais bien plus simple : voir TRANSLATING.md ou ouvrir un ticket Translation.
- Les termes du jeu (attributs, postes, qualité du pied, personnalités, temps de jeu, etc.) suivent désormais les textes officiels de chaque version linguistique du jeu.
- Les noms et abréviations des rôles FM27 suivent la langue de l'interface, formulés comme dans cette version linguistique du jeu. Vos tactiques et paramètres restent inchangés.
- Les valeurs des joueurs suivent de même la langue de l'interface : attributs, personnalité, qualité du pied, pied préféré et postes.
- Les montants peuvent s'afficher en livres, dollars américains, euros, francs suisses, yuans chinois ou roubles russes (Langue → Devise), convertis avec les taux de change du jeu. Par défaut, la devise correspond à la langue de l'interface. Les valeurs enregistrées restent inchangées.
- Pour FM27, l'import propose désormais d'abord la sauvegarde. Les exports HTML/CSV fonctionnent toujours comme avant.
- Les exports HTML/CSV sont lus dans toutes les langues de jeu prises en charge (anglais, allemand, français, italien, chinois simplifié, russe, espagnol). La langue est reconnue d'après les en-têtes de colonnes ; elle peut aussi être définie dans Paramètres → Import.
- Vérification des mises à jour facultative, désactivée par défaut : l'application demande une fois si elle peut chercher de nouvelles versions au démarrage. Elle demande seulement à GitHub la dernière version et n'envoie aucune donnée sur vous ou vos sauvegardes.
- Une nouvelle version s'affiche comme indication avec ses changements et un bouton de téléchargement qui ouvre la page de la version. Rien n'est installé automatiquement ; une version peut être ignorée.
- Paramètres → Sauvegardes → Mises à jour : activer ou désactiver la vérification, inclure les versions de test ou vérifier maintenant.
- Après une mise à jour, l'application affiche une fois les nouveautés, dans la langue de l'interface. « Ne plus afficher » désactive cela (aussi dans Paramètres → Sauvegardes → Mises à jour) ; Aide → Nouveautés les affiche à tout moment. L'indication de mise à jour montre elle aussi les changements de la nouvelle version dans la langue de l'interface.
- Meilleur XI FM27 : un joueur peut dépanner à une place hors de ses postes dans l'une des deux compositions, pour que des places ne restent plus vides simplement parce que personne n'a les deux postes. Sa valeur y est réduite (facteur 0,80, Paramètres → Effectif et analyse) ; le rôle apparaît hachuré.
- Les joueurs sont aussi évalués pour des rôles qui ne leur sont pas attribués quand la composition en a besoin.
- Vue combinée : une place libre avec et sans ballon s'affiche dans une seule case, et les places libres ne tracent plus de lignes de liaison.
